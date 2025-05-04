#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "utils/slice.h"
#include "utils/status.h"
#include "utils/log.h"

using namespace std;
namespace LSMKV {
  static inline uint64_t DecodeFixed64BigEnd(const char *ptr) {
      uint64_t value{};
      memcpy(&value, ptr, sizeof(uint64_t));
      if constexpr (std::endian::native == std::endian::little) {
          value = std::byteswap(value);
      }
      return value;
  }

  static inline double DecodeDouble(const char *ptr) {
      double value{};
      memcpy(&value, ptr, sizeof(double));
      return value;
  }

  struct SegmentWSeq {
      static constexpr uint64_t blob_size = 8 + 8 + 16 + 8;
      uint64_t count = 0;
      double sum_high = 0;
      double sum_low = 0;
      double sum_y = 0;
      double sum_high2 = 0;
      double sum_low2 = 0;
      double sum_high_low = 0;
      double sum_high_y = 0;
      double sum_low_y = 0;

      uint64_t k = 0;
      uint64_t m = 0;
      double a = 0;
      double b = 0;

      std::vector<std::tuple<uint64_t, uint64_t, uint32_t>> points;

      std::string Encode() {
          std::string result;
          result.reserve(64);
          result.append(reinterpret_cast<const char *>(&k), sizeof(k));
          result.append(reinterpret_cast<const char *>(&m), sizeof(m));
          result.append(reinterpret_cast<const char *>(&a), sizeof(a));
          result.append(reinterpret_cast<const char *>(&b), sizeof(b));
          return result;
      }

      void Clear() {
          count = 0;
          sum_high = 0;
          sum_low = 0;
          sum_y = 0;
          sum_high2 = 0;
          sum_low2 = 0;
          sum_high_low = 0;
          sum_high_y = 0;
          sum_low_y = 0;

          k = 0;
          m = 0;
          a = 0;
          b = 0;

          points.clear();
      }

      bool update(uint64_t high, uint64_t low, uint32_t y);
  };

  struct Linear {
      double a;
      double k;
      static constexpr uint32_t linear_params_size = 16;
      static constexpr uint32_t model_block_size = 20;
      static constexpr uint32_t linear_start = 16 + 12;
      static constexpr uint32_t blob_size = 40;

      Linear(Slice key) {
          a = DecodeDouble(key.data());
          k = DecodeDouble(key.data() + 8);
      }

      Linear(double aa, double kk) : a(aa), k(kk) {}

      int32_t Predict(uint64_t key) const {
          return std::round(a * (key) + k);
      }

  };

  struct Segment {
      double sum_x = 0.0;
      double sum_y = 0.0;
      double sum_x_squared = 0.0;
      double sum_xy = 0.0;
      double comp_x = 0.0;
      double comp_y = 0.0;
      double comp_x2 = 0.0;
      double comp_xy = 0.0;
      double a = 0.0;
      double k = 0.0;

      bool is_init{false};

      std::vector<std::pair<uint64_t, uint32_t>> points;

      std::string Encode() {
          std::string result;
          result.resize(16);
          memcpy(result.data(), &a, sizeof(a));
          memcpy(result.data() + 8, &k, sizeof(k));
          return result;
      }

      void Clear() {
          sum_x = 0.0;
          sum_y = 0.0;
          sum_x_squared = 0.0;
          sum_xy = 0.0;
          comp_x = 0.0;
          comp_y = 0.0;
          comp_x2 = 0.0;
          comp_xy = 0.0;
          a = 0.0;
          k = 0.0;
          is_init = false;

          points.clear();
      }

      void updateSums(uint64_t x, uint32_t y) {
          double y_x = static_cast<double>(x) - comp_x;
          double t_x = sum_x + y_x;
          comp_x = (t_x - sum_x) - y_x;
          sum_x = t_x;

          double y_y = static_cast<double>(y) - comp_y;
          double t_y = sum_y + y_y;
          comp_y = (t_y - sum_y) - y_y;
          sum_y = t_y;

          double x_dbl = static_cast<double>(x);
          double x_sq = x_dbl * x_dbl;
          double y_x2 = x_sq - comp_x2;
          double t_x2 = sum_x_squared + y_x2;
          comp_x2 = (t_x2 - sum_x_squared) - y_x2;
          sum_x_squared = t_x2;

          double xy = x_dbl * static_cast<double>(y);
          double y_xy = xy - comp_xy;
          double t_xy = sum_xy + y_xy;
          comp_xy = (t_xy - sum_xy) - y_xy;
          sum_xy = t_xy;
      }

      bool update(uint64_t x, uint32_t y) {
          is_init = true;
          points.emplace_back(x, y);
          auto p_k = k;
          auto p_a = a;

          updateSums(x, y);
          auto count = points.size();
          switch (count) {
              case 1: {
                  a = 0.0;
                  k = static_cast<double>(points[0].second);
              }
                  break;
              case 2: {
                  auto dy = static_cast<double>(points[1].second) - static_cast<double>(points[0].second);
                  auto dx = static_cast<double>(points[1].first) - static_cast<double>(points[0].first);
                  a = dy / dx;
                  k = static_cast<double>(points[0].second) - a * static_cast<double>(points[0].first);
              }
                  break;
              default: {
                  const double det = sum_x_squared * count - sum_x * sum_x;
                  if (det != 0) {
                      a = (sum_xy * count - sum_x * sum_y) / det;
                      k = (sum_y - a * sum_x) / count;
                  } else {
                      double mean_x = sum_x / count;
                      double mean_y = sum_y / count;

                      double Sxx = sum_x_squared - sum_x * mean_x;
                      double Sxy = sum_xy - sum_x * mean_y;

                      if (Sxx == 0) {
                          a = 0.0;
                          k = mean_y;
                      } else {
                          a = Sxy / Sxx;
                          k = mean_y - a * mean_x;
                      }
                  }
              }
                  break;
          }

          for (const auto &point: points) {
              const auto [x, y] = point;
              const double error = std::abs(y - (a * x) - k);
              if (static_cast<uint64_t>(error) >= 2 * 40) {
                  k = p_k;
                  a = p_a;
                  return false;
              }
          }
          return true;
      }
  };

  class GreedyPLRTrainer {
  private:
      SegmentWSeq current_segment;
      std::string rep;
      uint32_t cur_size = 0;
      size_t last_start = 0;
      std::string last_key;

      size_t count;
      size_t num;

  public:
      explicit GreedyPLRTrainer() {}

      void Flush() {

      }

      void End() {
          if (cur_size == 0) {
              return;
          }

          memcpy(rep.data() + last_start, last_key.c_str(), last_key.size());

          memcpy(rep.data() + last_start + last_key.size(), &cur_size, sizeof(cur_size));

          cur_size = 0;
      }

      void Add(Slice key, uint32_t y) {
          uint64_t high = DecodeFixed64BigEnd(key.data());
          uint64_t low = DecodeFixed64(key.data() + 8) >> 8;

          if (!current_segment.update(high, low, y)) {
              auto encoded = current_segment.Encode();
              if (cur_size == 0) {
                  rep.resize(rep.size() + 16 + 4 + 4 + 4);
              }
              rep.append(encoded);
              cur_size++;
              count += current_segment.count;
              num++;
              log::info("per segment count {}", count / num);
              current_segment.Clear();
              assert(current_segment.update(high, low, y));
          }
      }
  };

  class LazyGreedyPLRTrainer {
      struct BlockTrainer {
          struct Points {
              uint64_t key;
              uint32_t y;
          };

          void Add(Slice key, uint32_t y) {
              uint64_t high = DecodeFixed64BigEnd(key.data());
              uint64_t low = DecodeFixed64(key.data() + 8);
              points.emplace_back(Points{.key = high, .y = y});
          }

          Slice Encode() {
              return rep_;
          }

          void Train();

          std::string rep_;

          std::vector<Points> points;
      };

      bool is_init = false;
      std::vector<BlockTrainer> trainers;
  public:

      void End() {
          if (!is_init) {
              return;
          }
          is_init = false;
      }

      std::string rep_;

      void Train() {
          for (auto &trainer: trainers) {
              trainer.Train();
              rep_.append(trainer.Encode());
          }
          trainers.clear();
//          log::info("model size per sst: {}", rep_.size());
      }


      void Add(Slice key, uint32_t y) {
          if (!is_init) {
              trainers.emplace_back();
              is_init = true;
          }
          trainers.back().Add(key, y);
      }

  };

} // namespace LSMKV