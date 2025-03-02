#include <gtest/gtest.h>
#include "include/lsmkv/kvstore_api.h"  // 你的 LSM-Tree 引擎头文件
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include "src/include/kvstore.h"

#include <cmath>
#include <random>

template<typename IntType = int>
class zipf_distribution {
public:
    explicit zipf_distribution(IntType n = 100, double a = 0.99)
            : n(n), a(a), inv_1ma_(1.0 / (1.0 - a)), H_n_(0.0) {
        prob_cache_.reserve(n);
        for (IntType k = 1; k <= n; ++k) {
            prob_cache_.push_back(1.0 / std::pow(k, a));
        }
        H_n_ = std::accumulate(prob_cache_.begin(), prob_cache_.end(), 0.0);
    }

    template<typename Generator>
    IntType operator()(Generator &gen) {
        std::uniform_real_distribution<double> unif{0.0, 1.0};
        while (true) {
            const double u = 0.5 + unif(gen) * (1.0 - 1e-10);
            const double uz = u * H_n_;

            if (uz < 1.0) return 0;
            if (uz < 1.0 + prob_cache_[0]) return 1;

            const double v = (H_n_ - uz) * (a - 1.0);
            auto k = static_cast<IntType>(std::pow(v, inv_1ma_));
            k = std::min(k, n);

            if (k >= 1 && (prob_cache_[k - 1] / H_n_) >= unif(gen)) {
                return k;
            }
        }
    }

    double s() const { return a; }

    IntType max() const { return n - 1; }

    IntType min() const { return 0; }

private:
    IntType n;
    double a;
    double inv_1ma_;
    double H_n_;
    std::vector<double> prob_cache_;
};


class YCSBBenchmark : public ::testing::Test {
protected:
    static const int kNumKeys = 10000;
    static const int kValueSize = 1024;
    static const int kNumThreads = 8;

    struct OperationStats {
        std::atomic<int64_t> total_ops{0};
        std::atomic<int64_t> total_latency_ns{0};
        std::vector<int64_t> latency_samples;
    };

    void SetUp() override {
        auto status = KVStore::Open("/tmp/data", "/tmp/data", &db_);
    }

    void TearDown() override {
//        utils::rmfiles("/tmp/data");
    }

    class WorkloadGenerator {
    public:
        enum Distribution { UNIFORM, ZIPFIAN };

        WorkloadGenerator(int num_keys, Distribution dist)
                : gen_(std::random_device()()),
                  uniform_dist_(0, num_keys - 1),
                  zipf_dist_() {
            distribution_ = dist;
        }

        uint64_t NextKey() {
            if (distribution_ == ZIPFIAN) {
                return zipf_dist_(gen_);
            } else {
                return uniform_dist_(gen_);
            }
        }

    private:
        std::mt19937 gen_;
        Distribution distribution_;
        std::uniform_int_distribution<uint64_t> uniform_dist_;
        zipf_distribution<uint64_t> zipf_dist_;
    };

    std::string Key(uint64_t i) {
        std::string key;
        key.resize(8);
        LSMKV::EncodeFixed64(&key[0], std::byteswap(i));
        return key;
    }

    void Reset() {
        db_->reset();
        PrepareTestData();
    }

    void RunWorkload(int read_ratio, int write_ratio) {
        std::vector<std::thread> threads;
        OperationStats global_stats;


        std::array<std::vector<int64_t>, kNumThreads> latency_samples;
        auto start = std::chrono::high_resolution_clock::now();


        for (int i = 0; i < kNumThreads; ++i) {
            threads.emplace_back([&, i]() {
                OperationStats local_stats;
                WorkloadGenerator wg(kNumKeys, WorkloadGenerator::ZIPFIAN);
                std::string value(kValueSize, 'a'); // 测试用固定值

                for (int j = 0; j < kNumKeys / kNumThreads * 10; ++j) {
                    auto key = wg.NextKey();
                    auto op_start = std::chrono::high_resolution_clock::now();

                    // 执行读写操作
                    if ((j % (read_ratio + write_ratio)) < read_ratio) {
                        db_->get(Key(key));
                    } else {
                        db_->put(Key(key), value);
                    }

                    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::high_resolution_clock::now() - op_start);

                    local_stats.total_ops++;
                    local_stats.total_latency_ns += duration.count();
                    local_stats.latency_samples.push_back(duration.count());
                }

                // 合并统计结果
                global_stats.total_ops += local_stats.total_ops;
                global_stats.total_latency_ns += local_stats.total_latency_ns;
                latency_samples[i] = std::move(local_stats.latency_samples);
            });
        }

        for (auto &t: threads) t.join();
        auto end = std::chrono::high_resolution_clock::now();

        for (auto &v: latency_samples) {
            global_stats.latency_samples.insert(global_stats.latency_samples.end(), v.begin(), v.end());
        }


        auto total_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double throughput = (global_stats.total_ops * 1e9) / total_time_ns;
        double avg_latency = (global_stats.total_latency_ns * 1.0) / global_stats.total_ops;

        std::sort(global_stats.latency_samples.begin(), global_stats.latency_samples.end());
        double p99 = global_stats.latency_samples[global_stats.latency_samples.size() * 0.99];


        std::cout << "\n=== performance ==="
                  << "\nthroughput: " << throughput << " ops/sec"
                  << "\naverage latency " << avg_latency / 1e3 << " μs"
                  << "\nP99 latency: " << p99 / 1e3 << " μs"
                  << std::endl;
    }

private:
    std::unique_ptr<KVStoreAPI> db_;

    void PrepareTestData() {
        std::string value(kValueSize, 'a');
        for (uint64_t i = 0; i < kNumKeys; ++i) {
            db_->put(Key(i), value);
        }

        LSMKV::log::info("data is ready");
    }
};

TEST_F(YCSBBenchmark, WriteHeavy) {
    Reset();
    RunWorkload(10, 90);
}

TEST_F(YCSBBenchmark, ReadHeavy) {
    Reset();
    RunWorkload(90, 10);
}

TEST_F(YCSBBenchmark, Balanced) {
    Reset();
    RunWorkload(50, 50);
}
