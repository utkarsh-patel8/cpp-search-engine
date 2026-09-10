#include "search_engine.hpp"

#include <fstream>
#include <iostream>
#include <system_error>

bool indexes_are_equal(
    const InvertedIndex &first,
    const InvertedIndex &second)
{
    if (first.size() != second.size())
        return false;

    for (const auto &entry : first)
    {
        const std::string &term = entry.first;
        const std::vector<Posting> &first_postings = entry.second;

        auto second_entry = second.find(term);

        if (second_entry == second.end())
            return false;

        const std::vector<Posting> &second_postings =
            second_entry->second;

        if (first_postings.size() != second_postings.size())
        {
            return false;
        }

        for (
            std::size_t posting_index = 0;
            posting_index < first_postings.size();
            posting_index++)
        {
            const Posting &first_posting =
                first_postings[posting_index];

            const Posting &second_posting =
                second_postings[posting_index];

            if (
                first_posting.document_id !=
                    second_posting.document_id ||
                first_posting.term_frequency !=
                    second_posting.term_frequency ||
                first_posting.positions !=
                    second_posting.positions)
            {
                return false;
            }
        }
    }

    return true;
}

bool documents_are_equal(
    const std::vector<Document> &first,
    const std::vector<Document> &second)
{
    if (first.size() != second.size())
    {
        return false;
    }

    for (
        std::size_t document_index = 0;
        document_index < first.size();
        document_index++)
    {
        if (
            first[document_index].id !=
                second[document_index].id ||
            first[document_index].path !=
                second[document_index].path ||
            first[document_index].token_count !=
                second[document_index].token_count)
        {
            return false;
        }
    }

    return true;
}

bool run_self_tests(){
    std::size_t passed_test_count = 0;
    std::size_t failed_test_count = 0;

    auto check = [&](
        bool condition,
        const std::string& test_name
    ){
        if(condition){
            std::cout
                << "[PASS] "
                << test_name
                << '\n';

            passed_test_count++;
        } else{
            std::cout
                << "[FAIL] "
                << test_name
                << '\n';

            failed_test_count++;
        }
    };

    /*
     * Test 1: tokenization
     */
    std::vector<std::string> actual_tokens =
        tokenize("Hello, C++ WORLD_42!");

    std::vector<std::string> expected_tokens = {
        "hello",
        "c",
        "world",
        "42"
    };

    check(
        actual_tokens == expected_tokens,
        "Tokenizer lowercases and separates tokens"
    );

    /*
     * Test 2: term frequency and positions
     */
    Document test_document{
        0,
        "test_document.txt",
        3
    };

    InvertedIndex small_index;

    add_document_to_index(
        test_document,
        {"database", "system", "database"},
        small_index
    );

    auto database_entry =
        small_index.find("database");

    bool posting_is_correct =
        database_entry != small_index.end() &&
        database_entry->second.size() == 1 &&
        database_entry->second[0].document_id == 0 &&
        database_entry->second[0].term_frequency == 2 &&
        database_entry->second[0].positions ==
            std::vector<std::size_t>{0, 2};

    check(
        posting_is_correct,
        "Index stores term frequency and positions"
    );

    /*
     * Create a unique temporary directory for integration tests.
     */
    std::string unique_directory_name =
        "cpp_search_engine_tests_" +
        std::to_string(
            PerformanceClock::now()
                .time_since_epoch()
                .count()
        );

    fs::path test_directory =
        fs::temp_directory_path() /
        unique_directory_name;

    std::error_code filesystem_error;

    fs::create_directories(
        test_directory,
        filesystem_error
    );

    if(filesystem_error){
        std::cerr
            << "Could not create temporary test directory: "
            << filesystem_error.message()
            << '\n';

        return false;
    }

    fs::path first_path =
        test_directory / "document_0.txt";

    fs::path second_path =
        test_directory / "document_1.txt";

    fs::path third_path =
        test_directory / "document_2.txt";

    /*
     * Braces ensure all files are closed before indexing starts.
     */
    {
        std::ofstream first_file(first_path);
        std::ofstream second_file(second_path);
        std::ofstream third_file(third_path);

        first_file
            << "Database systems support query optimization.";

        second_file
            << "Operating system process scheduling.";

        third_file
            << "Database process database query.";
    }

    std::vector<Document> original_documents = {
        {0, first_path, 0},
        {1, second_path, 0},
        {2, third_path, 0}
    };

    std::vector<Document> serial_documents =
        original_documents;

    std::vector<Document> parallel_documents =
        original_documents;

    InvertedIndex serial_index;
    InvertedIndex parallel_index;

    std::size_t serial_document_count = 0;
    std::size_t serial_token_count = 0;

    std::size_t parallel_document_count = 0;
    std::size_t parallel_token_count = 0;

    std::vector<fs::path> serial_failures;
    std::vector<fs::path> parallel_failures;

    build_index_single_threaded(
        serial_documents,
        serial_index,
        serial_document_count,
        serial_token_count,
        serial_failures
    );

    std::size_t used_thread_count =
        build_index_multithread(
            parallel_documents,
            parallel_index,
            parallel_document_count,
            parallel_token_count,
            parallel_failures,
            3
        );

    check(
        serial_failures.empty() &&
        parallel_failures.empty(),
        "All test documents are readable"
    );

    check(
        used_thread_count == 3,
        "Parallel builder uses requested thread count"
    );

    check(
        serial_document_count ==
            parallel_document_count &&
        serial_token_count ==
            parallel_token_count,
        "Serial and parallel statistics match"
    );

    check(
        documents_are_equal(
            serial_documents,
            parallel_documents
        ),
        "Serial and parallel document metadata match"
    );

    check(
        indexes_are_equal(
            serial_index,
            parallel_index
        ),
        "Serial and parallel indexes match completely"
    );

    /*
     * Test 3: AND query
     *
     * Documents 0 and 2 contain both database and query.
     */
    std::vector<int> and_results =
        search_all_terms(
            {"database", "query"},
            serial_index
        );

    check(
        and_results == std::vector<int>{0, 2},
        "AND search returns the correct documents"
    );

    /*
     * Test 4: exact phrase
     *
     * Only document 2 contains the adjacent phrase
     * "database query".
     */
    std::vector<int> phrase_results =
        search_exact_phrase(
            {"database", "query"},
            serial_index
        );

    check(
        phrase_results == std::vector<int>{2},
        "Phrase search checks adjacent positions"
    );

    /*
     * Test 5: BM25 ranking
     *
     * Document 2 contains database twice and should rank
     * above document 0.
     */
    double average_document_length =
        static_cast<double>(serial_token_count) /
        static_cast<double>(serial_document_count);

    std::vector<SearchResult> ranked_results =
        search_ranked(
            {"database", "query"},
            serial_index,
            serial_documents,
            serial_document_count,
            average_document_length,
            10
        );

    check(
        !ranked_results.empty() &&
        ranked_results.front().document_id == 2,
        "BM25 ranks the stronger match first"
    );

    /*
     * Test 6: persistence round trip
     */
    fs::path saved_test_index =
        test_directory / "test_index.bin";

    bool save_succeeded =
        save_index(
            saved_test_index,
            serial_documents,
            serial_index,
            serial_document_count,
            serial_token_count
        );

    std::vector<Document> loaded_documents;
    InvertedIndex loaded_index;

    std::size_t loaded_document_count = 0;
    std::size_t loaded_token_count = 0;

    bool load_succeeded =
        load_index(
            saved_test_index,
            loaded_documents,
            loaded_index,
            loaded_document_count,
            loaded_token_count
        );

    check(
        save_succeeded && load_succeeded,
        "Index can be saved and loaded"
    );

    check(
        loaded_document_count ==
            serial_document_count &&
        loaded_token_count ==
            serial_token_count &&
        documents_are_equal(
            serial_documents,
            loaded_documents
        ) &&
        indexes_are_equal(
            serial_index,
            loaded_index
        ),
        "Loaded index exactly matches saved index"
    );

    /*
     * Remove only the uniquely named temporary test directory.
     */
    filesystem_error.clear();

    fs::remove_all(
        test_directory,
        filesystem_error
    );

    if(filesystem_error){
        std::cerr
            << "Warning: Could not remove temporary test files: "
            << filesystem_error.message()
            << '\n';
    }

    std::cout
        << "\nTests passed: "
        << passed_test_count
        << '\n';

    std::cout
        << "Tests failed: "
        << failed_test_count
        << '\n';

    return failed_test_count == 0;
}
