#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct Document
{
    int id;
    fs::path path;
    std::size_t token_count;
};

struct Posting
{
    int document_id;
    int term_frequency;
    std::vector<std::size_t> positions;
};

struct SearchResult
{
    int document_id;
    double score;
};

using InvertedIndex =
    std::unordered_map<
        std::string,
        std::vector<Posting>
    >;

using PerformanceClock = std::chrono::steady_clock;

double elapsed_milliseconds(
    const PerformanceClock::time_point& start_time,
    const PerformanceClock::time_point& end_time
);

bool is_supported_file(const fs::path& file_path);

bool read_file(
    const fs::path& file_path,
    std::string& content
);

std::vector<std::string> tokenize(
    const std::string& text
);

void add_document_to_index(
    const Document& document,
    const std::vector<std::string>& tokens,
    InvertedIndex& inverted_index
);

void build_index_single_threaded(
    std::vector<Document>& documents,
    InvertedIndex& inverted_index,
    std::size_t& indexed_document_count,
    std::size_t& total_token_count,
    std::vector<fs::path>& failed_files
);

std::size_t build_index_multithread(
    std::vector<Document>& documents,
    InvertedIndex& inverted_index,
    std::size_t& indexed_document_count,
    std::size_t& total_token_count,
    std::vector<fs::path>& failed_files,
    std::size_t requested_thread_count
);

std::vector<int> search_exact_phrase(
    const std::vector<std::string>& phrase_tokens,
    const InvertedIndex& inverted_index
);

std::vector<int> search_all_terms(
    const std::vector<std::string>& query_tokens,
    const InvertedIndex& inverted_index
);

std::vector<SearchResult> search_ranked(
    const std::vector<std::string>& query_tokens,
    const InvertedIndex& inverted_index,
    const std::vector<Document>& documents,
    std::size_t indexed_document_count,
    double average_document_length,
    std::size_t maximum_results
);

std::vector<Document> discover_documents(
    const fs::path& folder_path
);

std::string generate_snippet(
    const std::string& content,
    const std::vector<std::string>& query_tokens,
    std::size_t context_characters = 90
);

bool save_index(
    const fs::path& index_file_path,
    const std::vector<Document>& documents,
    const InvertedIndex& inverted_index,
    std::size_t indexed_document_count,
    std::size_t total_token_count
);

bool load_index(
    const fs::path& index_file_path,
    std::vector<Document>& documents,
    InvertedIndex& inverted_index,
    std::size_t& indexed_document_count,
    std::size_t& total_token_count
);

bool run_self_tests();