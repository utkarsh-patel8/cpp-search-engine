#include "search_engine.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>


int main(int argc, char* argv[])
{

    if(argc==2 && std::string(argv[1])=="--test"){
        return run_self_tests() ? 0:1;
    }

    if(argc!=1){
        std::cerr << "Usage:\n" << "  search_engine.exe\n" << "  search_engine.exe --test\n";
        return 1;
    }

    std::vector<Document> documents;
    InvertedIndex inverted_index;

    std::size_t indexing_thread_count = 1;
    std::size_t indexed_document_count = 0;
    std::size_t total_token_count = 0;
    double discovery_time_ms = 0.0;
    double indexing_time_ms = 0.0;
    double saving_time_ms = 0.0;
    double loading_time_ms = 0.0;

    const fs::path saved_index_path = "search_index.bin";

    std::cout << "Choose an option\n";
    std::cout << "1. Build a new index\n";
    std::cout << "2. Load the saved index\n";
    std::cout << "Enter choice: ";

    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "1")
    {
        std::cout << "Enter the folder to index: ";

        std::string input_path;
        std::getline(std::cin, input_path);

        // To remove any quotation marks
        if (input_path.size() >= 2 &&
            input_path.front() == '"' &&
            input_path.back() == '"')
        {

            input_path = input_path.substr(1, input_path.size() - 2);
        }

        // Convert the input path to filesystem path, gives useful ops like filename, extension etc.
        fs::path folder_path = input_path;

        // If the path does not exist then throw error
        if (!fs::exists(folder_path))
        {
            std::cerr << "Error: The path does not exist. \n";
            return 1;
        }

        // If the path is not a directory path
        if (!fs::is_directory(folder_path))
        {
            std::cerr << "Error: The given path is not a directory. \n";
            return 1;
        }

        // Calling our fn to discover the documents
        auto discovery_start_time = PerformanceClock::now();

        documents = discover_documents(folder_path);

        auto discovery_end_time = PerformanceClock::now();

        discovery_time_ms = elapsed_milliseconds(discovery_start_time, discovery_end_time);

        // Print no of supported documents found
        std::cout << "\n Discovered " << documents.size() << " supported documents.\n\n";

        unsigned int detected_hardware_threads = std::thread::hardware_concurrency();

        std::cout << "\nDetected logical processors: " << detected_hardware_threads << '\n';
        std::cout
            << "Enter indexing thread count "
            << "(1 = serial, 0 = automatic): ";

        std::string thread_count_input;
        std::getline(std::cin, thread_count_input);

        try
        {
            if (thread_count_input == "0")
            {
                indexing_thread_count =
                    detected_hardware_threads == 0
                        ? 2
                        : detected_hardware_threads;
            }
            else
            {
                indexing_thread_count =
                    static_cast<std::size_t>(
                        std::stoull(thread_count_input));
            }
        }
        catch (const std::exception &)
        {
            std::cerr << "Invalid thread count.\n";
            return 1;
        }

        if (indexing_thread_count == 0)
        {
            std::cerr << "Invalid thread count.\n";
            return 1;
        }

        if (!documents.empty())
        {
            indexing_thread_count = std::min(
                indexing_thread_count,
                documents.size());
        }


        std::vector<fs::path> failed_files;
        auto indexing_start_time = PerformanceClock::now();

        if (indexing_thread_count == 1)
        {
            build_index_single_threaded(
                documents, inverted_index, indexed_document_count, total_token_count, failed_files);
        }
        else
        {
            indexing_thread_count = build_index_multithread(
                documents, inverted_index, indexed_document_count, total_token_count,
                failed_files, indexing_thread_count);
        }

        auto indexing_end_time = PerformanceClock::now();

        indexing_time_ms = elapsed_milliseconds(
            indexing_start_time,
            indexing_end_time);

        // Print failures after timing so that terminal output does not distort the indexing benchmark
        for (const fs::path &failed_file : failed_files)
        {
            std::cerr
                << "Could not read: "
                << failed_file.string()
                << '\n';
        }

        auto saving_start_time = PerformanceClock::now();

        bool index_saved = save_index(
            saved_index_path,
            documents,
            inverted_index,
            indexed_document_count,
            total_token_count);

        auto saving_end_time = PerformanceClock::now();

        saving_time_ms = elapsed_milliseconds(saving_start_time, saving_end_time);

        if (index_saved)
        {
            std::cout << "\nIndex saved to " << saved_index_path.string() << ".\n";
        }
        else
        {
            std::cerr << "\nWarning: Could not save the index.\n";
        }
    }

    else if (choice == "2")
    {

        auto loading_start_time = PerformanceClock::now();

        bool index_loaded = load_index(saved_index_path, documents,
                                       inverted_index, indexed_document_count, total_token_count);

        auto loading_end_time = PerformanceClock::now();

        loading_time_ms = elapsed_milliseconds(loading_start_time, loading_end_time);

        if (!index_loaded)
        {
            std::cerr << "Error: Could not load " << saved_index_path.string() << ".\n";

            std::cerr << "Build a new index first using option 1.\n";
            return 1;
        }

        std::cout << "\nSaved index loaded successfully.\n";
    }
    else
    {
        std::cerr << "Invalid choice.\n";
        return 1;
    }

    double average_document_length = 0.0;

    if (indexed_document_count > 0)
    {
        average_document_length =
            static_cast<double>(total_token_count) /
            static_cast<double>(indexed_document_count);
    }

    std::cout << "\nIndex ready.\n";
    std::cout << "Documents indexed: " << indexed_document_count << '\n';
    std::cout << "Unique terms: " << inverted_index.size() << '\n';
    std::cout << "Average document length: "
              << std::fixed
              << std::setprecision(2)
              << average_document_length
              << " tokens\n";

    std::cout << "\nPerformance metrics:\n";

    if (choice == "1")
        std::cout
            << "Indexing threads: "
            << indexing_thread_count
            << '\n';

    std::cout
        << std::fixed
        << std::setprecision(3);

    if (choice == "1")
    {
        std::cout
            << "Document discovery: "
            << discovery_time_ms
            << " ms\n";

        std::cout
            << "Index construction: "
            << indexing_time_ms
            << " ms\n";

        std::cout
            << "Index saving: "
            << saving_time_ms
            << " ms\n";

        if (indexing_time_ms > 0.0)
        {
            double indexing_seconds =
                indexing_time_ms / 1000.0;

            double documents_per_second =
                static_cast<double>(
                    indexed_document_count) /
                indexing_seconds;

            double tokens_per_second =
                static_cast<double>(
                    total_token_count) /
                indexing_seconds;

            std::cout
                << "Indexing throughput: "
                << documents_per_second
                << " documents/second\n";

            std::cout
                << "Token throughput: "
                << tokens_per_second
                << " tokens/second\n";
        }
    }
    else
    {
        std::cout
            << "Index loading: "
            << loading_time_ms
            << " ms\n";
    }

    //  file_size() can report an error through error_code instead of throwing an exception.

    std::error_code file_size_error;

    std::uintmax_t index_size_bytes =
        fs::file_size(
            saved_index_path,
            file_size_error);

    if (!file_size_error)
    {
        double index_size_kilobytes =
            static_cast<double>(index_size_bytes) /
            1024.0;

        std::cout
            << "Saved index size: "
            << index_size_kilobytes
            << " KiB\n";
    }

    while (true)
    {
        std::cout
            << "\nEnter search query "
            << "(:quit to exit): ";

        std::string query;

        // getline returns false if the input stream closes or fails. This can happen when the user
        // sends an end-of-file signal.

        if (!std::getline(std::cin, query))
        {
            std::cout << "\nInput stream closed.\n";
            break;
        }

        // These are commands understood by our CLI rather than search queries.

        if (query == ":quit" || query == ":exit")
        {
            break;
        }

        // The complete query must be enclosed in quotes for exact phrase search.

        bool is_exact_phrase_query =
            query.size() >= 2 &&
            query.front() == '"' &&
            query.back() == '"';

        if (is_exact_phrase_query)
        {
            query = query.substr(
                1,
                query.size() - 2);
        }

        std::vector<std::string> query_tokens =
            tokenize(query);

        if (query_tokens.empty())
        {
            std::cout
                << "The query contains no searchable words.\n";

            // Do not terminate the program. Return to the beginning of the while loop and ask for another query.

            continue;
        }

        const std::size_t maximum_results = 10;

        if (is_exact_phrase_query)
        {

            auto query_start_time = PerformanceClock::now();

            std::vector<int> matching_document_ids = search_exact_phrase(query_tokens, inverted_index);

            auto query_end_time = PerformanceClock::now();

            double query_time_ms = elapsed_milliseconds(query_start_time, query_end_time);

            if (matching_document_ids.empty())
            {
                std::cout
                    << "No documents contain the exact phrase.\n";

                std::cout << "Retrieval time: " << std::fixed << std::setprecision(3) << query_time_ms << " ms\n";

                continue;
            }

            std::cout
                << "\nFound "
                << matching_document_ids.size()
                << " exact phrase result(s):\n\n";

            std::size_t results_to_display = std::min(
                matching_document_ids.size(),
                maximum_results);

            for (
                std::size_t result_index = 0;
                result_index < results_to_display;
                result_index++)
            {
                int document_id =
                    matching_document_ids[result_index];

                const Document &document =
                    documents[document_id];

                std::cout
                    << result_index + 1
                    << ". "
                    << document.path.string()
                    << '\n';

                std::string document_content;

                if (read_file(
                        document.path,
                        document_content))
                {
                    std::string snippet =
                        generate_snippet(
                            document_content,
                            query_tokens);

                    if (!snippet.empty())
                    {
                        std::cout
                            << "    Snippet: "
                            << snippet
                            << '\n';
                    }
                }

                std::cout << '\n';
            }

            /*
             * Phrase-search output is complete. Begin the next loop
             * iteration instead of also running BM25.
             */

            std::cout
                << "Retrieval time: "
                << std::fixed
                << std::setprecision(3)
                << query_time_ms
                << " ms\n";

            continue;
        }

        auto query_start_time = PerformanceClock::now();

        std::vector<SearchResult> results =
            search_ranked(
                query_tokens,
                inverted_index,
                documents,
                indexed_document_count,
                average_document_length,
                maximum_results);

        auto query_end_time = PerformanceClock::now();

        double query_time_ms = elapsed_milliseconds(query_start_time, query_end_time);

        if (results.empty())
        {
            std::cout << "No matching documents found.\n";

            std::cout
                << "Retrieval time: "
                << std::fixed
                << std::setprecision(3)
                << query_time_ms
                << " ms\n";

            continue;
        }

        std::cout
            << "\nTop "
            << results.size()
            << " result(s):\n\n";

        for (
            std::size_t result_index = 0;
            result_index < results.size();
            result_index++)
        {
            const SearchResult &result =
                results[result_index];

            const Document &document =
                documents[result.document_id];

            std::cout
                << result_index + 1
                << ". "
                << document.path.string()
                << '\n';

            std::cout
                << "    BM25 Score: "
                << std::fixed
                << std::setprecision(4)
                << result.score
                << '\n';

            std::string document_content;

            if (
                read_file(
                    document.path,
                    document_content))
            {
                std::string snippet =
                    generate_snippet(
                        document_content,
                        query_tokens);

                if (!snippet.empty())
                {
                    std::cout
                        << "    Snippet: "
                        << snippet
                        << '\n';
                }
            }

            std::cout << '\n';
        }

        std::cout
            << "Retrieval time: "
            << std::fixed
            << std::setprecision(3)
            << query_time_ms
            << " ms\n";
    }

    std::cout << "Search engine closed.\n";

    return 0;
}