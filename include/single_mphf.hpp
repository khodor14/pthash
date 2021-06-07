#pragma once

#include "utils/bucketers.hpp"
#include "builders/util.hpp"
#include "builders/internal_memory_builder_single_mphf.hpp"
#include "builders/external_memory_builder_single_mphf.hpp"

namespace pthash {

template <typename Hasher, typename Encoder>
struct single_mphf {
    typedef Encoder encoder_type;

    template <typename Iterator>
    build_timings build_in_internal_memory(Iterator keys, uint64_t n,
                                           build_configuration const& config) {
        internal_memory_builder_single_mphf<Hasher> builder;
        auto timings = builder.build_from_keys(keys, n, config);
        timings.encoding_seconds = build(builder, config);
        return timings;
    }

    template <typename Iterator>
    build_timings build_in_external_memory(Iterator keys, uint64_t n,
                                           build_configuration const& config) {
        external_memory_builder_single_mphf<Hasher> builder;
        auto timings = builder.build_from_keys(keys, n, config);
        timings.encoding_seconds = build(builder, config);
        return timings;
    }

    template <typename Builder>
    double build(Builder const& builder, build_configuration const&) {
        auto start = clock_type::now();
        m_seed = builder.seed();
        m_num_keys = builder.num_keys();
        m_table_size = builder.table_size();
        m_M = fastmod::computeM_u64(m_table_size);
        m_bucketer = builder.bucketer();
        m_pilots.encode(builder.pilots().data(), m_bucketer.num_buckets());
        m_free_slots.encode(builder.free_slots().data(), m_table_size - m_num_keys);
        auto stop = clock_type::now();
        return seconds(stop - start);
    }

    template <typename T>
    uint64_t operator()(T const& key) const {
        auto hash = Hasher::hash(key, m_seed);
        return position(hash);
    }

    uint64_t position(typename Hasher::hash_type hash) const {
        uint64_t bucket = m_bucketer.bucket(hash.first());
        uint64_t pilot = m_pilots.access(bucket);
        uint64_t hashed_pilot = default_hash64(pilot, m_seed);
        uint64_t p = fastmod::fastmod_u64(hash.second() ^ hashed_pilot, m_M, m_table_size);
        if (PTHASH_LIKELY(p < num_keys())) return p;
        return m_free_slots.access(p - num_keys());
    }

    size_t num_bits_for_pilots() const {
        return 8 * (sizeof(m_seed) + sizeof(m_num_keys) + sizeof(m_table_size) + sizeof(m_M)) +
               m_bucketer.num_bits() + m_pilots.num_bits();
    }

    size_t num_bits_for_mapper() const {
        return m_free_slots.num_bits();
    }

    size_t num_bits() const {
        return num_bits_for_pilots() + num_bits_for_mapper();
    }

    inline uint64_t num_keys() const {
        return m_num_keys;
    }

    template <typename Visitor>
    void visit(Visitor& visitor) {
        visitor.visit(m_seed);
        visitor.visit(m_num_keys);
        visitor.visit(m_table_size);
        visitor.visit(m_M);
        visitor.visit(m_bucketer);
        visitor.visit(m_pilots);
        visitor.visit(m_free_slots);
    }

private:
    uint64_t m_seed;
    uint64_t m_num_keys;
    uint64_t m_table_size;
    __uint128_t m_M;
    skew_bucketer m_bucketer;
    Encoder m_pilots;
    ef_sequence<false> m_free_slots;
};

}  // namespace pthash