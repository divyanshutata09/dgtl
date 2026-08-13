/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
// // #include <exception>
// // #include <iostream>

// // #include "build/DGTL_SDK/include/dgtl.h"

// // int main() {
// //     try {
// //         std::cout
// //             << "DGTL version: "
// //             << dgtl_version_string()
// //             << '\n';

// //         auto* context = dgtl_context_create();
// //         if (!context) {
// //             std::cout << "Context FAIL\n";
// //             return 1;
// //         }

// //         std::cout << "Context OK\n";
// //         std::cout << "Before create\n";

// //         auto* stream =
// //             dgtl_stream_create(context, 16);

// //         std::cout << "After create\n";

// //         if (stream)
// //             std::cout << "Stream OK\n";
// //         else
// //             std::cout << "Stream FAIL\n";

// //         dgtl_stream_destroy(stream);
// //         dgtl_context_destroy(context);

// //         std::cout << "Destroy OK\n";
// //     }
// //     catch (const std::exception& e) {
// //         std::cout
// //             << "Exception: "
// //             << e.what()
// //             << '\n';
// //     }
// //     catch (...) {
// //         std::cout
// //             << "Unknown exception\n";
// //     }
// // }

// #include <exception>
// #include <iostream>

// #include "build/DGTL_SDK/include/dgtl.h"

// int main() {
//     try {
//         std::cout
//             << "DGTL version: "
//             << dgtl_version_string()
//             << '\n';

//         auto* context = dgtl_context_create();
//         if (!context) {
//             std::cout << "Context FAIL\n";
//             return 1;
//         }

//         std::cout << "Context OK\n";

//         auto* stream =
//             dgtl_stream_create(context, 16);

//         if (!stream) {
//             std::cout << "Stream FAIL\n";
//             dgtl_context_destroy(context);
//             return 1;
//         }

//         std::cout << "Stream OK\n";

//         // Emit constants
//         auto lhs =
//             dgtl_emit_const_i64(stream, 10);

//         auto rhs =
//             dgtl_emit_const_i64(stream, 20);

//         // Emit add
//         auto result =
//             dgtl_emit_add_i64(
//                 stream,
//                 lhs,
//                 rhs
//             );

//         std::cout
//             << "LHS node: "
//             << lhs
//             << '\n';

//         std::cout
//             << "RHS node: "
//             << rhs
//             << '\n';

//         std::cout
//             << "ADD node: "
//             << result
//             << '\n';

//         auto node_count =
//             dgtl_stream_node_count(stream);

//         std::cout
//             << "Node count: "
//             << node_count
//             << '\n';

//         dgtl_stream_destroy(stream);
//         dgtl_context_destroy(context);

//         std::cout << "Destroy OK\n";
//     }
//     catch (const std::exception& e) {
//         std::cout
//             << "Exception: "
//             << e.what()
//             << '\n';
//     }
//     catch (...) {
//         std::cout
//             << "Unknown exception\n";
//     }

//     return 0;
// }

#include <exception>
#include <iostream>

#include "build/DGTL_SDK/include/dgtl.h"

int main() {
    try {
        auto* context =
            dgtl_context_create();

        if (!context) {
            std::cout << "Context FAIL\n";
            return 1;
        }

        auto* stream =
            dgtl_stream_create(context, 16);

        if (!stream) {
            std::cout << "Stream FAIL\n";
            dgtl_context_destroy(context);
            return 1;
        }

        auto lhs =
            dgtl_emit_const_i64(stream, 10);

        auto rhs =
            dgtl_emit_const_i64(stream, 20);

        auto result =
            dgtl_emit_add_i64(
                stream,
                lhs,
                rhs
            );

        std::cout
            << "Before optimize: "
            << dgtl_stream_node_count(stream)
            << '\n';

        dgtl_status_t optimize_status =
            dgtl_optimize(stream);

        std::cout
            << "Optimize result: "
            << optimize_status.ok
            << '\n';

        if (!optimize_status.ok) {
            std::cout
                << "Optimize message: "
                << optimize_status.message
                << '\n';
        }

        std::cout
            << "After optimize: "
            << dgtl_stream_node_count(stream)
            << '\n';

        dgtl_stream_destroy(stream);
        dgtl_context_destroy(context);

        std::cout << "Destroy OK\n";
    }
    catch (const std::exception& e) {
        std::cout
            << "Exception: "
            << e.what()
            << '\n';
        return 1;
    }

    return 0;
}