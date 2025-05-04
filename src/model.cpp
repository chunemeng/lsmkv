#include "include/model.h"

namespace LSMKV {
  bool SegmentWSeq::update(uint64_t high, uint64_t low, uint32_t y) {

      points.emplace_back(high, low, y);
      auto p_k = k;
      auto p_m = m;
      auto p_a = a;
      auto p_b = b;

      sum_high += static_cast<double>(high);
      sum_low += static_cast<double>(low);
      sum_y += static_cast<double>(y);
      sum_high2 += static_cast<double>(high) * high;
      sum_low2 += static_cast<double>(low) * low;
      sum_high_low += static_cast<double>(high) * low;
      sum_high_y += static_cast<double>(high) * y;
      sum_low_y += static_cast<double>(low) * y;
      count++;
      switch (count) {
          case 1: {
              a = 1;
              k = high - y;
              b = 0;
          }
              break;
          case 2: {
              const auto &p1 = points[0];
              const auto &p2 = points[1];
              auto dy = std::get<2>(p2) - std::get<2>(p1);
              if (std::get<0>(p1) == std::get<0>(p2)) {
                  auto dx = std::get<1>(p2) - std::get<1>(p1);
                  a = 0;

                  b = dy / dx;
                  m = (b * (double) std::get<0>(p1) - (double) std::get<2>(p1)) / b;
              } else {
                  auto dx = std::get<0>(p2) - std::get<0>(p1);

                  a = dy / dx;

                  k = (a * (double) std::get<0>(p1) - (double) std::get<2>(p1)) / a;
                  b = 0;
              }
          }
              break;
          default: {
              k = static_cast<uint64_t>(sum_high / count);
              m = static_cast<uint64_t>(sum_low / count);

              const double Sx1x1 = sum_high2 - sum_high * sum_high / count;
              const double Sx2x2 = sum_low2 - sum_low * sum_low / count;
              const double Sx1x2 = sum_high_low - sum_high * sum_low / count;
              const double Sx1y = sum_high_y - sum_high * sum_y / count;
              const double Sx2y = sum_low_y - sum_low * sum_y / count;

              const double det = Sx1x1 * Sx2x2 - Sx1x2 * Sx1x2;
              if (det != 0) {
                  a = (Sx2x2 * Sx1y - Sx1x2 * Sx2y) / det;
                  b = (-Sx1x2 * Sx1y + Sx1x1 * Sx2y) / det;
              } else {
                  const double lambda = 1e-6;
                  const double Sx1x1_reg = Sx1x1 + lambda;
                  const double Sx2x2_reg = Sx2x2 + lambda;
                  const double det_reg = Sx1x1_reg * Sx2x2_reg - Sx1x2 * Sx1x2;
                  if (det_reg != 0) {
                      a = (Sx2x2_reg * Sx1y - Sx1x2 * Sx2y) / det_reg;
                      b = (-Sx1x2 * Sx1y + Sx1x1_reg * Sx2y) / det_reg;
                  } else {
                      a = (Sx1y != 0) ? Sx1y / Sx1x1_reg : 0;
                      b = 0;
                  }
              }
          }
      }


      for (const auto &point: points) {
          const auto [h, l, y] = point;
          const double error = std::abs(y - (a * (h - k) + b * (l - m)));
          if (static_cast<uint64_t>(error) > 8 * blob_size) {
              k = p_k;
              m = p_m;
              a = p_a;
              b = p_b;
              assert(points.size() > 5);
//                  log::info("error: {}, k: {}, m: {}, a: {}, b: {}", error, k, m, a, b);
//                  log::info("p_k: {}, p_m: {}, p_a: {}, p_b: {}", p_k, p_m, p_a, p_b);
              return false;
          }
      }
      return true;
  }

  void LazyGreedyPLRTrainer::BlockTrainer::Train() {
      rep_.resize(8 + 4 + 4 + 4);
      uint32_t size = 0;

      {
          auto back = points.back();
          EncodeFixed64(rep_.data(), std::byteswap(back.key));
          EncodeFixed32(rep_.data() + 8, size);
          EncodeFixed32(rep_.data() + 12, points.front().y);
          EncodeFixed32(rep_.data() + 16, points.back().y);
      }
      Segment current_segment;
      for (auto i = 0; i < points.size(); ++i) {
          auto [key, y] = points[i];
          if (!current_segment.update(key, y)) {
              auto encoded = current_segment.Encode();
              rep_.append(encoded);
              size++;
              current_segment.Clear();
              assert(current_segment.update(key, y));
          }
      }
      if (current_segment.is_init) {
          auto encoded = current_segment.Encode();
          rep_.append(encoded);
          size++;
      }

      EncodeFixed32(rep_.data() + 8, size);
  }

} // namespace LSMKV