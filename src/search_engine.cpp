#include "search_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>

struct PartialIndexResult
{
    InvertedIndex inverted_index;
    std::size_t indexed_document_count = 0;
    std::size_t total_token_count = 0;
    std::vector<fs::path> failed_files;
};


double elapsed_milliseconds(
    const PerformanceClock::time_point &start_time,
    const PerformanceClock::time_point &end_time)
{
    return std::chrono::duration<
               double,
               std::milli>(end_time - start_time)
        .count();
}

template <typename NumberType>
bool write_binary_number(
    std::ostream &output,
    const NumberType &value)
{
    output.write(
        reinterpret_cast<const char *>(&value),
        sizeof(NumberType));

    return static_cast<bool>(output);
}

template <typename NumberType>
bool read_binary_number(
    std::istream &input,
    NumberType &value)
{
    input.read(
        reinterpret_cast<char *>(&value),
        sizeof(NumberType));

    return static_cast<bool>(input);
}

bool write_binary_string(
    std::ostream &output,
    const std::string &value)
{
    std::uint64_t length = static_cast<std::uint64_t>(value.size());

    if (!write_binary_number(output, length))
    {
        return false;
    }

    if (length > 0)
    {
        output.write(
            value.data(),
            static_cast<std::streamsize>(length));
    }

    return static_cast<bool>(output);
}

bool read_binary_string(
    std::istream &input,
    std::string &value)
{
    std::uint64_t length = 0;

    if (!read_binary_number(input, length))
    {
        return false;
    }

    // Prevent a corrupted file asking the program to allocate an unreasonable amount of memory
    const std::uint64_t maximum_string_length = 10ULL * 1024ULL * 1024ULL;

    if (length > maximum_string_length)
    {
        return false;
    }

    value.resize(static_cast<std::size_t>(length));

    if (length > 0)
    {
        input.read(
            value.data(),
            static_cast<std::streamsize>(length));
    }

    return static_cast<bool>(input);
}

bool is_supported_file(const fs::path &file_path)
{ // Pass by reference, fn should not modify path

    // Static so that this set is constructed only once
    static const std::unordered_set<std::string> supported_extensions = {
        ".txt", ".md", ".cpp", ".c", ".h", ".hpp"};

    // .extension() returns path, so we convert it to string
    std::string extension = file_path.extension().string();

    // Convert extension to lowercase
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                   { return std::tolower(character); });

    return supported_extensions.count(extension) > 0;
}

bool read_file(const fs::path &file_path, std::string &content)
{
    std::ifstream file(file_path);

    if (!file.is_open())
    {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    content = buffer.str();
    return true;
}

// This fn returns vectors of tokens
std::vector<std::string> tokenize(const std::string &text)
{

    std::vector<std::string> tokens;
    std::string current_token;

    for (unsigned char character : text)
    {
        if (std::isalnum(character))
        {
            current_token += static_cast<char>(std::tolower(character));
        }

        else if (!current_token.empty())
        {
            tokens.push_back(current_token);
            current_token.clear();
        }
    }

    if (!current_token.empty())
    {
        tokens.push_back(current_token);
    }

    return tokens;
}

void add_document_to_index(
    const Document &document,
    const std::vector<std::string> &tokens,
    InvertedIndex &inverted_index)
{
    // We do this loop because we need posn of each token as well
    for (std::size_t token_position = 0; token_position < tokens.size(); token_position++)
    {
        const std::string &token = tokens[token_position];

        std::vector<Posting> &postings = inverted_index[token];

        if (postings.empty() || postings.back().document_id != document.id)
        {
            // First occurrence in the current document
            postings.push_back({document.id, 1, {token_position}});
        }

        else
        {
            // Word already appeared in this document, add the token posn in the most recent posting's posn vector
            postings.back().term_frequency++;
            postings.back().positions.push_back(token_position);
        }
    }
}

void build_index_single_threaded(
    std::vector<Document> &documents,
    InvertedIndex &inverted_index,
    std::size_t &indexed_document_count,
    std::size_t &total_token_count,
    std::vector<fs::path> &failed_files)
{
    inverted_index.clear();
    indexed_document_count = 0;
    total_token_count = 0;
    failed_files.clear();

    for (Document &document : documents)
    {
        std::string content;

        if (!read_file(document.path, content))
        {
            failed_files.push_back(document.path);
            continue;
        }

        std::vector<std::string> tokens = tokenize(content);

        document.token_count = tokens.size();
        indexed_document_count++;
        total_token_count += tokens.size();

        add_document_to_index(
            document,
            tokens,
            inverted_index);
    }
}

// What one worker thread does
void index_document_range(
    std::vector<Document> &documents,
    std::size_t range_begin,
    std::size_t range_end,
    PartialIndexResult &partial_result)
{
    for (
        std::size_t document_index = range_begin;
        document_index < range_end;
        document_index++)
    {
        Document &document = documents[document_index];
        std::string content;

        if (!read_file(document.path, content))
        {
            partial_result.failed_files.push_back(document.path);
            continue;
        }

        std::vector<std::string> tokens = tokenize(content);

        document.token_count = tokens.size();
        partial_result.indexed_document_count++;
        partial_result.total_token_count += tokens.size();

        add_document_to_index(
            document,
            tokens,
            partial_result.inverted_index);
    }
}

std::size_t build_index_multithread(
    std::vector<Document> &documents,
    InvertedIndex &inverted_index,
    std::size_t &indexed_document_count,
    std::size_t &total_token_count,
    std::vector<fs::path> &failed_files,
    std::size_t requested_thread_count)
{
    inverted_index.clear();
    indexed_document_count = 0;
    total_token_count = 0;
    failed_files.clear();

    if (documents.empty())
    {
        return 0;
    }

    // More threads than documents would be pointless
    std::size_t thread_count = std::min(requested_thread_count, documents.size());

    // We must alawys use atleast one thread
    thread_count = std::max(thread_count, static_cast<std::size_t>(1));

    // One private result for every thread
    std::vector<PartialIndexResult> partial_results(thread_count);

    // Stores the actual worker thread objects
    std::vector<std::thread> worker_threads;

    worker_threads.reserve(thread_count);

    for (std::size_t thread_index = 0; thread_index < thread_count; thread_index++)
    {
        std::size_t range_begin = documents.size() * thread_index / thread_count;
        std::size_t range_end = documents.size() * (thread_index + 1) / thread_count;

        worker_threads.emplace_back(
            index_document_range,
            std::ref(documents),
            range_begin,
            range_end,
            std::ref(partial_results[thread_index]));
    }

    // Wait for every thread to finish
    for (std::thread &worker_thread : worker_threads)
    {
        worker_thread.join();
    }

    // Estimate how many map entries may be needed
    std::size_t estimated_term_entries = 0;

    for (const PartialIndexResult &partial_result : partial_results)
    {
        estimated_term_entries += partial_result.inverted_index.size();
    }

    inverted_index.reserve(estimated_term_entries);

    // Workers have finished, so the main thread can now merge their results safely
    for (
        std::size_t thread_index = 0; thread_index < thread_count; thread_index++)
    {
        PartialIndexResult &partial_result = partial_results[thread_index];
        indexed_document_count += partial_result.indexed_document_count;

        total_token_count += partial_result.total_token_count;

        failed_files.insert(
            failed_files.end(),
            std::make_move_iterator(
                partial_result.failed_files.begin()),
            std::make_move_iterator(
                partial_result.failed_files.end()));

        for (auto &index_entry : partial_result.inverted_index)
        {
            const std::string &term = index_entry.first;
            std::vector<Posting> &source_postings = index_entry.second;
            std::vector<Posting> &desination_postings = inverted_index[term];

            desination_postings.reserve(
                desination_postings.size() + source_postings.size());

            desination_postings.insert(
                desination_postings.end(),
                std::make_move_iterator(
                    source_postings.begin()),
                std::make_move_iterator(
                    source_postings.end()));
        }
    }

    return thread_count;
}

const Posting *find_posting_for_document(
    const std::vector<Posting> &postings,
    int document_id)
{
    // Posting lists are sorted by Document IDs
    // Lower bound to find the first posting whose document ID >= document_id
    auto posting_iterator = std::lower_bound(
        postings.begin(),
        postings.end(),
        document_id,
        [](
            const Posting &posting,
            int wanted_document_id)
        {
            return posting.document_id < wanted_document_id;
        });

    // The document does not occur in the posting list
    if (posting_iterator == postings.end() || posting_iterator->document_id != document_id)
    {
        return nullptr;
    }

    // Return the address of the posting that was found
    return &(*posting_iterator);
}

std::vector<int> search_exact_phrase(
    const std::vector<std::string> &phrase_tokens,
    const InvertedIndex &inverted_index)
{
    if (phrase_tokens.empty())
    {
        return {};
    }

    // Every matching document must contain the first word
    // So its posting list gives us the candidate documents

    auto first_term_entry = inverted_index.find(phrase_tokens[0]);

    if (first_term_entry == inverted_index.end())
    {
        return {};
    }

    std::vector<int> matching_document_ids;

    // Test every document containing the first word
    for (
        const Posting &first_term_posting : first_term_entry->second)
    {
        bool phrase_found_in_document = false;

        // The first word may occur multiple times.
        // Each occurrence is a possible starting position for the phrase.

        for (std::size_t starting_position : first_term_posting.positions)
        {

            // For now assume all words match the positions
            bool all_positions_match = true;

            // Start at 1 because phase_token[0] is known to occur at starting position in this document

            for (std::size_t term_index = 1; term_index < phrase_tokens.size(); term_index++)
            {

                auto term_entry = inverted_index.find(phrase_tokens[term_index]);

                // If the second word does occur then return
                if (term_entry == inverted_index.end())
                {
                    return {};
                }

                const Posting *term_posting = find_posting_for_document(term_entry->second,
                                                                        first_term_posting.document_id);

                // If this returns nullptr then the word does not exist in this document
                if (term_posting == nullptr)
                {
                    all_positions_match = false;
                    break;
                }

                // The required position of this word will be
                std::size_t required_position = starting_position + term_index;

                bool position_exists = std::binary_search(
                    term_posting->positions.begin(),
                    term_posting->positions.end(),
                    required_position);

                if (!position_exists)
                {
                    all_positions_match = false;
                    break;
                }
            }

            if (all_positions_match)
            {
                matching_document_ids.push_back(
                    first_term_posting.document_id);

                phrase_found_in_document = true;
                break;
            }
        }

        if (phrase_found_in_document)
        {
            continue;
        }
    }

    return matching_document_ids;
}

// This function is used to find intersection bw two postings for multiword AND queries
std::vector<int> intersect_with_postings(
    const std::vector<int> &current_results,
    const std::vector<Posting> &postings)
{
    std::vector<int> intersection;

    std::size_t result_index = 0;
    std::size_t posting_index = 0;

    while (result_index < current_results.size() &&
           posting_index < postings.size())
    {

        int result_document_id = current_results[result_index];
        int posting_document_id = postings[posting_index].document_id;

        if (result_document_id == posting_document_id)
        {
            intersection.push_back(result_document_id);

            result_index++;
            posting_index++;
        }

        else if (result_document_id < posting_document_id)
        {
            result_index++;
        }

        else
            posting_index++;
    }

    return intersection;
}

// Multiword search
std::vector<int> search_all_terms(
    const std::vector<std::string> &query_tokens,
    const InvertedIndex &inverted_index)
{
    if (query_tokens.empty())
    {
        return {};
    }

    auto first_entry = inverted_index.find(query_tokens[0]);

    if (first_entry == inverted_index.end())
    {
        return {};
    }

    std::vector<int> matching_document_ids;

    for (const Posting &posting : first_entry->second)
    {
        matching_document_ids.push_back(posting.document_id);
    }

    for (std::size_t token_index = 1; token_index < query_tokens.size(); token_index++)
    {

        auto index_entry = inverted_index.find(query_tokens[token_index]);

        if (index_entry == inverted_index.end())
        {
            return {};
        }

        matching_document_ids = intersect_with_postings(matching_document_ids, index_entry->second);

        if (matching_document_ids.empty())
        {
            return {};
        }
    }

    return matching_document_ids;
}

struct MinScoreComparator
{
    bool operator()(
        const SearchResult &first,
        const SearchResult &second) const
    {
        if (first.score != second.score)
        {
            return first.score > second.score;
        }

        return first.document_id < second.document_id;
    }
};

std::vector<SearchResult> search_ranked(
    const std::vector<std::string> &query_tokens,
    const InvertedIndex &inverted_index,
    const std::vector<Document> &documents,
    std::size_t indexed_document_count,
    double average_document_length,
    std::size_t maximum_results)
{

    if (query_tokens.empty() || indexed_document_count == 0 ||
        average_document_length <= 0.0 || maximum_results == 0)
    {
        return {};
    }

    std::unordered_map<int, double> document_scores;
    std::unordered_set<std::string> processed_terms;

    const double k1 = 1.5;
    const double b = 0.75;

    double total_documents = static_cast<double>(indexed_document_count);

    for (const std::string &term : query_tokens)
    {

        // If already processed this term then continue
        if (processed_terms.count(term) > 0)
        {
            continue;
        }

        // Otherwise process it
        processed_terms.insert(term);

        auto index_entry = inverted_index.find(term);

        if (index_entry == inverted_index.end())
        {
            continue;
        }

        const std::vector<Posting> &postings = index_entry->second;

        // No of documents in which the term appears
        double document_frequency = static_cast<double>(postings.size());

        // Calculating IDF
        double inverse_document_frequency = std::log(
            1.0 +
            (total_documents -
             document_frequency +
             0.5) /
                (document_frequency + 0.5));

        for (const Posting &posting : postings)
        {
            // TF
            double term_frequency =
                static_cast<double>(
                    posting.term_frequency);

            double document_length = static_cast<double>(
                documents[posting.document_id].token_count);

            double length_normalization =
                1.0 -
                b +
                b *
                    (document_length / average_document_length);

            double term_score =
                inverse_document_frequency *
                (term_frequency * (k1 + 1.0)) /
                (term_frequency +
                 k1 * length_normalization);

            document_scores[posting.document_id] += term_score;
        }
    }

    std::priority_queue<
        SearchResult,
        std::vector<SearchResult>,
        MinScoreComparator>
        top_reults;

    for (const auto &score_entry : document_scores)
    {
        SearchResult candidate = {
            score_entry.first,
            score_entry.second};

        if (top_reults.size() < maximum_results)
        {
            top_reults.push(candidate);
            continue;
        }

        const SearchResult &weakest_result = top_reults.top();

        bool candidate_is_better =
            candidate.score > weakest_result.score || (candidate.score == weakest_result.score &&
                                                       candidate.document_id < weakest_result.document_id);

        if (candidate_is_better)
        {
            top_reults.pop();
            top_reults.push(candidate);
        }
    }

    std::vector<SearchResult> results;

    while (!top_reults.empty())
    {
        results.push_back(top_reults.top());
        top_reults.pop();
    }

    std::reverse(results.begin(), results.end());

    return results;
}

std::vector<Document> discover_documents(const fs::path &folder_path)
{
    // Receives a folder & finds all the documents & returns in a vector

    std::vector<Document> documents;
    std::error_code error;

    // An iterator represents our current position during directory traversal.
    fs::recursive_directory_iterator current(
        folder_path,
        fs::directory_options::skip_permission_denied,
        error);

    // It represents the position after all files have been visited
    fs::recursive_directory_iterator end;

    //  Traversal Loop
    while (current != end)
    {
        // In case of errors

        if (error)
        {
            error.clear();            // Clear current error
            current.increment(error); // Move the iterator to next file/directory, if any error, store in error
            continue;
        }

        if (current->is_regular_file(error) && is_supported_file(current->path()))
        {
            // Document id is the current size of the documents array
            int document_id = static_cast<int>(documents.size());

            documents.push_back({document_id,
                                 current->path(),
                                 0});
        }

        current.increment(error);
    }

    return documents;
}

std::string generate_snippet(
    const std::string &content,
    const std::vector<std::string> &query_tokens,
    std::size_t context_characters)
{
    std::unordered_set<std::string> query_terms(
        query_tokens.begin(), query_tokens.end());

    std::string current_token;

    std::size_t token_start = 0;
    std::size_t match_position = std::string::npos;

    for (std::size_t index = 0; index <= content.size(); index++)
    {
        bool reached_end = (index == content.size());

        if (!reached_end)
        {
            unsigned char character = static_cast<unsigned char>(content[index]);

            if (std::isalnum(character))
            {
                if (current_token.empty())
                {
                    token_start = index;
                }

                current_token += static_cast<char>(std::tolower(character));
                continue;
            }
        }

        if (!current_token.empty())
        {
            if (query_terms.count(current_token) > 0)
            {
                match_position = token_start;
                break;
            }

            current_token.clear();
        }
    }

    if (match_position == std::string::npos)
    {
        return "";
    }

    std::size_t snippet_start = 0;

    if (match_position > context_characters)
    {
        snippet_start = match_position - context_characters;
    }

    std::size_t snippet_end = std::min(
        content.size(), match_position + context_characters);

    std::string raw_snippet = content.substr(
        snippet_start,
        snippet_end - snippet_start);

    std::string cleaned_snippet;
    bool previous_was_space = false;

    for (unsigned char character : raw_snippet)
    {
        if (std::isspace(character))
        {
            if (!previous_was_space && !cleaned_snippet.empty())
            {
                cleaned_snippet.push_back(' ');
            }

            previous_was_space = true;
        }

        else
        {
            cleaned_snippet.push_back(static_cast<char>(character));
            previous_was_space = false;
        }
    }

    if (!cleaned_snippet.empty() && cleaned_snippet.back() == ' ')
    {
        cleaned_snippet.pop_back();
    }

    if (snippet_start > 0)
    {
        cleaned_snippet = "..." + cleaned_snippet;
    }

    if (snippet_end < content.size())
    {
        cleaned_snippet += "...";
    }

    return cleaned_snippet;
}

bool save_index(
    const fs::path &index_file_path,
    const std::vector<Document> &documents,
    const InvertedIndex &inverted_index,
    std::size_t indexed_document_count,
    std::size_t total_token_count)
{
    std::ofstream output(
        index_file_path,
        std::ios::binary | std::ios::trunc);

    if (!output.is_open())
    {
        return false;
    }

    // File signature identifies the file as one created by our search engine
    const std::string file_signature = "CPPSEIDX";

    output.write(
        file_signature.data(),
        static_cast<std::streamsize>(file_signature.size()));

    // If we later change the saved formats, we can increase this number
    const std::uint32_t format_version = 1;

    if (!write_binary_number(output, format_version))
    {
        return false;
    }

    std::uint64_t saved_indexed_document_count = static_cast<std::uint64_t>(indexed_document_count);

    std::uint64_t saved_total_token_count = static_cast<std::uint64_t>(total_token_count);

    if (!write_binary_number(output, saved_indexed_document_count) ||
        !write_binary_number(output, saved_total_token_count))
    {
        return false;
    }

    // Save document metadata
    std::uint64_t document_count = static_cast<std::uint64_t>(documents.size());

    if (!write_binary_number(output, document_count))
    {
        return false;
    }

    for (const Document &document : documents)
    {
        std::int32_t document_id = static_cast<std::int32_t>(document.id);

        std::uint64_t token_count = static_cast<std::uint64_t>(document.token_count);

        if (!write_binary_number(output, document_id) || !write_binary_string(output, document.path.string()) ||
            !write_binary_number(output, token_count))
        {
            return false;
        }
    }

    // Save all terms and their posting lists
    std::uint64_t term_count = static_cast<std::uint64_t>(inverted_index.size());

    if (!write_binary_number(output, term_count))
    {
        return false;
    }

    for (const auto &index_entry : inverted_index)
    {
        const std::string &term = index_entry.first;
        const std::vector<Posting> &postings = index_entry.second;

        if (!write_binary_string(output, term))
        {
            return false;
        }

        std::uint64_t posting_count = static_cast<std::uint64_t>(postings.size());

        if (!write_binary_number(output, posting_count))
        {
            return false;
        }

        for (const Posting &posting : postings)
        {
            std::int32_t document_id = static_cast<std::int32_t>(posting.document_id);
            std::int32_t term_frequency = static_cast<std::int32_t>(posting.term_frequency);

            std::uint64_t position_count = static_cast<std::uint64_t>(posting.positions.size());

            if (
                !write_binary_number(output, document_id) ||
                !write_binary_number(output, term_frequency) ||
                !write_binary_number(output, position_count))
            {
                return false;
            }

            for (std::size_t position : posting.positions)
            {
                std::uint64_t saved_position = static_cast<std::uint64_t>(position);

                if (!write_binary_number(output, saved_position))
                    return false;
            }
        }
    }

    output.flush();
    return static_cast<bool>(output);
}

bool load_index(
    const fs::path &index_file_path,
    std::vector<Document> &documents,
    InvertedIndex &inverted_index,
    std::size_t &indexed_document_count,
    std::size_t &total_token_count)
{

    std::ifstream input(
        index_file_path,
        std::ios::binary);

    if (!input.is_open())
    {
        return false;
    }

    const std::string expected_signature = "CPPSEIDX";

    std::string loaded_signature(
        expected_signature.size(),
        '\0');

    input.read(
        loaded_signature.data(),
        static_cast<std::streamsize>(
            loaded_signature.size()));

    if (!input || loaded_signature != expected_signature)
    {
        return false;
    }

    std::uint32_t format_version = 0;

    if (
        !read_binary_number(input, format_version) ||
        format_version != 1)
    {
        return false;
    }

    std::uint64_t saved_indexed_document_count = 0;
    std::uint64_t saved_total_token_count = 0;

    if (
        !read_binary_number(
            input,
            saved_indexed_document_count) ||
        !read_binary_number(
            input,
            saved_total_token_count))
    {
        return false;
    }

    // Load into temporary containers first
    std::vector<Document> loaded_documents;
    InvertedIndex loaded_inverted_index;

    std::uint64_t document_count = 0;

    if (!read_binary_number(input, document_count))
    {
        return false;
    }

    if (
        document_count > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return false;
    }

    loaded_documents.reserve(static_cast<std::size_t>(document_count));

    for (std::uint64_t document_index = 0; document_index < document_count; document_index++)
    {
        std::int32_t document_id = 0;
        std::string document_path;
        std::uint64_t token_count = 0;

        if (
            !read_binary_number(input, document_id) ||
            !read_binary_string(input, document_path) ||
            !read_binary_number(input, token_count))
        {
            return false;
        }

        // Our program relies on documents[document_id]
        // Therefore, IDs must remain consecutive & equal to their vector indices

        if (
            document_id != static_cast<std::int32_t>(document_index))
        {
            return false;
        }

        loaded_documents.push_back({document_id,
                                    fs::path(document_path),
                                    static_cast<std::size_t>(token_count)});
    }

    std::uint64_t term_count = 0;

    if (!read_binary_number(input, term_count))
    {
        return false;
    }

    for (
        std::uint64_t term_index = 0; term_index < term_count; term_index++)
    {
        std::string term;
        std::uint64_t posting_count = 0;

        if (
            !read_binary_string(input, term) ||
            !read_binary_number(input, posting_count))
        {
            return false;
        }

        std::vector<Posting> &postings = loaded_inverted_index[term];

        postings.reserve(static_cast<std::size_t>(posting_count));

        for (
            std::uint64_t posting_index = 0;
            posting_index < posting_count;
            posting_index++)
        {
            std::int32_t document_id = 0;
            std::int32_t term_frequency = 0;
            std::uint64_t position_count = 0;

            if (
                !read_binary_number(input, document_id) ||
                !read_binary_number(input, term_frequency) ||
                !read_binary_number(input, position_count))
            {
                return false;
            }

            if (document_id < 0 || static_cast<std::uint64_t>(document_id) >= document_count ||
                term_frequency < 0 || static_cast<std::uint64_t>(term_frequency) != position_count)
            {
                return false;
            }

            Posting posting{
                document_id,
                term_frequency,
                {}};

            posting.positions.reserve(
                static_cast<std::size_t>(position_count));

            for (
                std::uint64_t position_index = 0;
                position_index < position_count;
                position_index++)
            {
                std::uint64_t position = 0;

                if (!read_binary_number(input, position))
                {
                    return false;
                }

                posting.positions.push_back(static_cast<std::size_t>(position));
            }

            postings.push_back(std::move(posting));
        }
    }

    if (saved_indexed_document_count > document_count)
    {
        return false;
    }

    // Loading succeeded completely. Move the temp containers into program's actual variables

    documents = std::move(loaded_documents);

    inverted_index = std::move(loaded_inverted_index);
    indexed_document_count = static_cast<std::size_t>(saved_indexed_document_count);

    total_token_count = static_cast<std::size_t>(saved_total_token_count);

    return true;
}
