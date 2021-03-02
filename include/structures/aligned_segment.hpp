#pragma once

#include <seqan3/alphabet/cigar/cigar.hpp>

#include "structures/breakend.hpp"          // for strand

/*! \brief Read segment aligned to the reference genome (part of chimeric/split-aligned read). Contains information
 *        parsed from the SA tag of an alignment in the SAM/BAM file.
 *
 * \param orientation   - mapping orientation (reverse or forward strand)
 * \param ref_name      - reference/chromosome name
 * \param pos           - start position of the alignment
 * \param mapq          - mapping quality
 * \param cig           - cigar string of the alignment
 */
struct AlignedSegment
{
    strand orientation;
    std::string ref_name;
    int32_t pos;
    int32_t mapq;
    std::vector<seqan3::cigar> cig;

    int32_t get_reference_start() const;

    int32_t get_reference_end() const;

    int32_t get_left_soft_clip() const;

    int32_t get_right_soft_clip() const;

    int32_t get_query_start() const;

    int32_t get_query_length() const;

    int32_t get_query_end() const;
};

template <typename stream_t>
inline stream_t operator<<(stream_t && stream, AlignedSegment const & a)
{
    stream << a.ref_name << ";"
           << a.get_reference_start() << "-" << a.get_reference_end() << ";"
           << a.get_query_start() << "-" << a.get_query_end() << ";"
           << ((a.orientation == strand::forward) ? "+" : "-") << ";"
           << a.mapq;
    return stream;
}

bool operator<(const AlignedSegment & lhs, const AlignedSegment & rhs);
