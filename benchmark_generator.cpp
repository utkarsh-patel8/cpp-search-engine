#include<chrono>
#include<filesystem>
#include<fstream>
#include<iostream>
#include<string>
#include<vector>

namespace fs = std::filesystem;

int main(
    int argument_count,
    char* argument_values[]
) {
    /*
     * Defaults:
     * 2,000 documents × approximately 500 words
     * = approximately 1,000,000 tokens.
     */
    std::size_t document_count = 2000;
    std::size_t words_per_document = 500;

    fs::path output_folder =
        "benchmark_documents";

    try {
        if (argument_count >= 2) {
            document_count =
                static_cast<std::size_t>(
                    std::stoull(argument_values[1])
                );
        }

        if (argument_count >= 3) {
            words_per_document =
                static_cast<std::size_t>(
                    std::stoull(argument_values[2])
                );
        }

        if (argument_count >= 4) {
            output_folder = argument_values[3];
        }
    } catch (const std::exception& error) {
        std::cerr
            << "Invalid command-line argument: "
            << error.what()
            << '\n';

        return 1;
    }

    if (
        document_count == 0 ||
        words_per_document == 0
    ) {
        std::cerr
            << "Document and word counts must be positive.\n";

        return 1;
    }

    /*
     * Do not silently overwrite an existing benchmark corpus.
     * Existing files could make the measured document count
     * different from the requested count.
     */
    if (fs::exists(output_folder)) {
        std::cerr
            << "Error: Output folder already exists: "
            << output_folder.string()
            << '\n';

        std::cerr
            << "Choose a different output folder name.\n";

        return 1;
    }

    std::error_code directory_error;

    fs::create_directories(
        output_folder,
        directory_error
    );

    if (directory_error) {
        std::cerr
            << "Could not create output folder: "
            << directory_error.message()
            << '\n';

        return 1;
    }

    /*
     * A fixed vocabulary makes generation deterministic.
     * It also creates posting lists of different sizes.
     */
    const std::vector<std::string> vocabulary = {
        "algorithm", "array", "binary", "cache",
        "compiler", "concurrency", "container", "cpu",
        "database", "document", "filesystem", "graph",
        "hash", "heap", "index", "iterator",
        "kernel", "latency", "memory", "network",
        "operating", "optimization", "pointer", "process",
        "query", "queue", "ranking", "recursion",
        "scheduler", "search", "serialization", "server",
        "sorting", "storage", "string", "system",
        "thread", "token", "tree", "vector",
        "virtual", "worker", "throughput", "position",
        "posting", "frequency", "relevance", "retrieval",
        "parallel", "synchronization", "mutex", "atomic",
        "buffer", "stream", "directory", "performance",
        "execution", "software", "structure", "function",
        "reference", "variable", "program", "language"
    };

    auto generation_start_time =
        std::chrono::steady_clock::now();

    for (
        std::size_t document_index = 0;
        document_index < document_count;
        document_index++
    ) {
        /*
         * Put only 100 files in each subdirectory.
         * This also exercises recursive directory traversal.
         */
        std::size_t directory_number =
            document_index / 100;

        fs::path document_directory =
            output_folder /
            (
                "group_" +
                std::to_string(directory_number)
            );

        fs::create_directories(
            document_directory,
            directory_error
        );

        if (directory_error) {
            std::cerr
                << "Could not create directory: "
                << directory_error.message()
                << '\n';

            return 1;
        }

        fs::path document_path =
            document_directory /
            (
                "document_" +
                std::to_string(document_index) +
                ".txt"
            );

        std::ofstream document_file(document_path);

        if (!document_file.is_open()) {
            std::cerr
                << "Could not create: "
                << document_path.string()
                << '\n';

            return 1;
        }

        /*
         * Give every document one unique numeric token. This
         * also creates many low-frequency index terms.
         */
        document_file
            << "document "
            << document_index
            << '\n';

        for (
            std::size_t word_index = 0;
            word_index < words_per_document;
            word_index++
        ) {
            /*
             * This arithmetic selects words deterministically.
             * There is no random-number generator, so the same
             * arguments always create the same corpus.
             */
            std::size_t vocabulary_index =
                (
                    document_index * 37 +
                    word_index * 17 +
                    (word_index / 11) * 13
                ) % vocabulary.size();

            document_file
                << vocabulary[vocabulary_index];

            /*
             * Create readable lines while preserving token order.
             */
            if ((word_index + 1) % 20 == 0) {
                document_file << '\n';
            } else {
                document_file << ' ';
            }
        }

        /*
         * Add guaranteed phrases for phrase-search testing.
         */
        if (document_index % 10 == 0) {
            document_file
                << "\noperating system scheduling\n";
        }

        if (document_index % 15 == 0) {
            document_file
                << "database query optimization\n";
        }

        if (document_index % 20 == 0) {
            document_file
                << "binary search tree\n";
        }
    }

    auto generation_end_time =
        std::chrono::steady_clock::now();

    double generation_time_seconds =
        std::chrono::duration<double>(
            generation_end_time -
            generation_start_time
        ).count();

    std::cout
        << "Generated "
        << document_count
        << " documents in "
        << generation_time_seconds
        << " seconds.\n";

    std::cout
        << "Output folder: "
        << output_folder.string()
        << '\n';

    std::cout
        << "Approximate base token count: "
        << document_count * words_per_document
        << '\n';

    return 0;
}