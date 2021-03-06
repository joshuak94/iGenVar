#include "modules/clustering/hierarchical_clustering_method.hpp"

#include <limits>                                                 // for infinity
#include <random>                                                 // for random_device

#include <seqan3/core/debug_stream.hpp>

#include "fastcluster.h"                                          // for hclust_fast

std::vector<std::vector<Junction>> partition_junctions(std::vector<Junction> const & junctions)
{
    // Partition based on mate 1
    std::vector<Junction> current_partition{};
    std::vector<std::vector<Junction>> current_partition_splitted;
    std::vector<std::vector<Junction>> final_partitions{};

    for (Junction const & junction : junctions)
    {
        if (current_partition.empty())
        {
            current_partition.push_back(junction);
        }
        else
        {
            if (junction.get_mate1().seq_name != current_partition.back().get_mate1().seq_name ||
                junction.get_mate1().orientation != current_partition.back().get_mate1().orientation ||
                abs(junction.get_mate1().position - current_partition.back().get_mate1().position) > 50)
            {
                // Partition based on mate 2
                std::sort(current_partition.begin(), current_partition.end(), [](Junction const & a, Junction const & b) {
                    return a.get_mate2() < b.get_mate2();
                });
                current_partition_splitted = split_partition_based_on_mate2(current_partition);
                for (std::vector<Junction> partition : current_partition_splitted)
                {
                    final_partitions.push_back(partition);
                }
                current_partition.clear();
            }
            current_partition.push_back(junction);
        }
    }
    if (!current_partition.empty())
    {
        std::sort(current_partition.begin(), current_partition.end(), [](Junction a, Junction b) {
            return a.get_mate2() < b.get_mate2();
        });
        current_partition_splitted = split_partition_based_on_mate2(current_partition);
        for (std::vector<Junction> partition : current_partition_splitted)
        {
            final_partitions.push_back(partition);
        }
    }
    return final_partitions;
}

std::vector<std::vector<Junction>> split_partition_based_on_mate2(std::vector<Junction> const & partition)
{
    std::vector<Junction> current_partition{};
    std::vector<std::vector<Junction>> splitted_partition{};

    for (Junction const & junction : partition)
    {
        if (current_partition.empty())
        {
            current_partition.push_back(junction);
        }
        else
        {
            if (junction.get_mate2().seq_name != current_partition.back().get_mate2().seq_name ||
                junction.get_mate2().orientation != current_partition.back().get_mate2().orientation ||
                abs(junction.get_mate2().position - current_partition.back().get_mate2().position) > 50)
            {
                std::sort(current_partition.begin(), current_partition.end());
                splitted_partition.push_back(current_partition);
                current_partition.clear();
            }
            current_partition.push_back(junction);
        }
    }
    if (!current_partition.empty())
    {
        std::sort(current_partition.begin(), current_partition.end());
        splitted_partition.push_back(current_partition);
    }
    return splitted_partition;
}

int junction_distance(Junction const & lhs, Junction const & rhs)
{
    if ((lhs.get_mate1().seq_name == rhs.get_mate1().seq_name) &&
        (lhs.get_mate1().orientation == rhs.get_mate1().orientation) &&
        (lhs.get_mate2().seq_name == rhs.get_mate2().seq_name) &&
        (lhs.get_mate2().orientation == rhs.get_mate2().orientation))
    {
        // Reference:                      ................
        // Junction 1 with mates A and B:     A------->B    (2bp inserted)
        // Junction 2 with mates C and D:    C------>D      (5bp inserted)
        // Distance = 1 (distance A-C) + 2 (distance B-D) + 3 (absolute insertion size difference)
        return (std::abs(lhs.get_mate1().position - rhs.get_mate1().position) +
                std::abs(lhs.get_mate2().position - rhs.get_mate2().position) +
                std::abs((int)(lhs.get_inserted_sequence().size() - rhs.get_inserted_sequence().size())));
    }
    else
    {
        return std::numeric_limits<int>::max();
    }
}

inline std::vector<Junction> subsample_partition(std::vector<Junction> const & partition, uint16_t const sample_size)
{
    assert(partition.size() >= sample_size);
    std::vector<Junction> subsample{};
    std::sample(partition.begin(), partition.end(), std::back_inserter(subsample),
                sample_size, std::mt19937{std::random_device{}()});
    return subsample;
}

std::vector<Cluster> hierarchical_clustering_method(std::vector<Junction> const & junctions,
                                                    double clustering_cutoff)
{
    auto partitions = partition_junctions(junctions);
    std::vector<Cluster> clusters{};
    // Set the maximum partition size that is still feasible to cluster in reasonable time
    // A trade-off between reducing runtime and keeping as many junctions as possible has to be made
    const size_t max_partition_size = 200;
    for (std::vector<Junction> & partition : partitions)
    {
        size_t partition_size = partition.size();
        if (partition_size < 2)
        {
            clusters.emplace_back(std::move(partition));
            continue;
        }
        if (partition_size > max_partition_size)
        {
            seqan3::debug_stream << "A partition exceeds the maximum size ("
                                 << partition_size
                                 << ">"
                                 << max_partition_size
                                 << ") and has to be subsampled. Representative partition member:\n["
                                 << partition[0].get_mate1()
                                 << "] -> ["
                                 << partition[0].get_mate2()
                                 << "]\n";
            partition = subsample_partition(partition, max_partition_size);
            partition_size = max_partition_size;
        }
        // Compute condensed distance matrix (upper triangle of the full distance matrix)
        std::vector<double> distmat ((partition_size * (partition_size - 1)) / 2);
        int k, i, j;
        for (i = k = 0; i < partition_size; ++i) {
            for (j = i + 1; j< partition_size; ++j) {
                // Compute distance between junctions i and j
                distmat[k] = junction_distance(partition[i], partition[j]);
                ++k;
            }
        }

        // Perform hierarchical clustering
        // `height` is filled with cluster distance for each step
        // `merge` contains dendrogram
        std::vector<int> merge (2 * (partition_size - 1));
        std::vector<double> height (partition_size - 1);
        hclust_fast(partition_size, distmat.data(), HCLUST_METHOD_AVERAGE, merge.data(), height.data());

        // Fill labels[i] with cluster label of junction i.
        // Clustering is stopped at step with cluster distance >= clustering_cutoff
        std::vector<int> labels (partition_size);
        cutree_cdist(partition_size, merge.data(), height.data(), clustering_cutoff, labels.data());

        std::unordered_map<int, std::vector<Junction>> label_to_junctions{};
        for (int i = 0; i < partition_size; ++i)
        {
            if (label_to_junctions.find(labels[i]) != label_to_junctions.end())
            {
                label_to_junctions[labels[i]].push_back(std::move(partition[i]));
            }
            else{
                label_to_junctions.emplace(labels[i], std::vector{std::move(partition[i])});
            }
        }

        // Add new clusters: junctions with the same label belong to one cluster
        for (auto & [lab, jun] : label_to_junctions )
        {
            std::sort(jun.begin(), jun.end());
            clusters.emplace_back(jun);
        }
    }
    std::sort(clusters.begin(), clusters.end());
    return clusters;
}
