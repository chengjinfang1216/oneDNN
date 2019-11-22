/*******************************************************************************
* Copyright 2019 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef EXAMPLE_UTILS_HPP
#define EXAMPLE_UTILS_HPP

#include <algorithm>
#include <assert.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <stdlib.h>
#include <string>

#include "dnnl.hpp"
#include "dnnl_debug.h"

dnnl::engine::kind validate_engine_kind(dnnl::engine::kind akind) {
    // Checking if a GPU exists on the machine
    if (akind == dnnl::engine::kind::gpu) {
        if (dnnl::engine::get_count(dnnl::engine::kind::gpu) == 0) {
            std::cout << "Application couldn't find GPU, please run with CPU "
                         "instead.\n";
            exit(0);
        }
    }
    return akind;
}

// Exception class to indicate that the example uses a feature that is not
// available on the current systems. It is not treated as an error then, but
// just notifies a user.
struct example_allows_unimplemented : public std::exception {
    example_allows_unimplemented(const char *message) noexcept
        : message(message) {}
    virtual const char *what() const noexcept override { return message; }
    const char *message;
};

// Runs example function with signature void() and catches errors.
// Returns `0` on success, `1` or DNNL error, and `2` on example error.
inline int handle_example_errors(std::function<void()> example) {
    int exit_code = 0;

    try {
        example();
    } catch (example_allows_unimplemented &e) {
        std::cout << e.message << std::endl;
        exit_code = 0;
    } catch (dnnl::error &e) {
        std::cout << "DNNL error caught: " << std::endl
                  << "\tStatus: " << dnnl_status2str(e.status) << std::endl
                  << "\tMessage: " << e.what() << std::endl;
        exit_code = 1;
    } catch (std::exception &e) {
        std::cout << "Error in the example: " << e.what() << std::endl;
        exit_code = 2;
    }

    std::cout << "Example " << (exit_code ? "failed" : "passed") << std::endl;
    return exit_code;
}

// Same as above, but for functions with signature void(int argc, char **argv).
inline int handle_example_errors(
        std::function<void(int, char **)> example, int argc, char **argv) {
    return handle_example_errors([&]() { example(argc, argv); });
}

// Same as above, but for functions with signature void(dnnl::engine::kind).
inline int handle_example_errors(
        std::function<void(dnnl::engine::kind)> example,
        dnnl::engine::kind engine_kind) {
    return handle_example_errors([&]() { example(engine_kind); });
}

inline dnnl::engine::kind parse_engine_kind(
        int argc, char **argv, int extra_args = 0) {
    // Returns default engine kind, i.e. CPU, if none given
    if (argc == 1) {
        return validate_engine_kind(dnnl::engine::kind::cpu);
    } else if (argc <= extra_args + 2) {
        std::string engine_kind_str = argv[1];
        // Checking the engine type, i.e. CPU or GPU
        if (engine_kind_str == "cpu") {
            return validate_engine_kind(dnnl::engine::kind::cpu);
        } else if (engine_kind_str == "gpu") {
            return validate_engine_kind(dnnl::engine::kind::gpu);
        }
    }

    // If all above fails, the example should be ran properly
    std::cout << "Inappropriate engine kind" << std::endl
              << "Please run example like this" << argv[0] << " [cpu|gpu]"
              << (extra_args ? " [extra arguments]" : "") << std::endl;
    exit(1);
}

// Read from memory, write to handle
inline void read_from_dnnl_memory(void *handle, dnnl::memory &mem) {
    dnnl::engine eng = mem.get_engine();
    size_t size = mem.get_desc().get_size();

#if DNNL_WITH_SYCL
    bool is_cpu_sycl = (DNNL_CPU_RUNTIME == DNNL_RUNTIME_SYCL
            && eng.get_kind() == dnnl::engine::kind::cpu);
    bool is_gpu_sycl = (DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
            && eng.get_kind() == dnnl::engine::kind::gpu);
    if (is_cpu_sycl || is_gpu_sycl) {
#ifdef DNNL_USE_SYCL_BUFFERS
        auto buffer = mem.get_sycl_buffer<uint8_t>();
        auto src = buffer.get_access<cl::sycl::access::mode::read>();
        uint8_t *src_ptr = src.get_pointer();
#elif defined(DNNL_USE_DPCPP_USM)
        uint8_t *src_ptr = (uint8_t *)mem.get_data_handle();
#else
#error "Not expected"
#endif
        for (size_t i = 0; i < size; ++i)
            ((uint8_t *)handle)[i] = src_ptr[i];
        return;
    }
#endif
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    if (eng.get_kind() == dnnl::engine::kind::gpu) {
        dnnl::stream s(eng);
        cl_command_queue q = s.get_ocl_command_queue();
        cl_mem m = mem.get_ocl_mem_object();

        cl_int ret = clEnqueueReadBuffer(
                q, m, CL_TRUE, 0, size, handle, 0, NULL, NULL);
        if (ret != CL_SUCCESS)
            throw std::runtime_error("clEnqueueReadBuffer failed");
        return;
    }
#endif

    if (eng.get_kind() == dnnl::engine::kind::cpu) {
        uint8_t *src = static_cast<uint8_t *>(mem.get_data_handle());
        for (size_t i = 0; i < size; ++i)
            ((uint8_t *)handle)[i] = src[i];
        return;
    }

    assert(!"not expected");
}

// Read from handle, write to memory
inline void write_to_dnnl_memory(void *handle, dnnl::memory &mem) {
    dnnl::engine eng = mem.get_engine();
    size_t size = mem.get_desc().get_size();

#if DNNL_WITH_SYCL
    bool is_cpu_sycl = (DNNL_CPU_RUNTIME == DNNL_RUNTIME_SYCL
            && eng.get_kind() == dnnl::engine::kind::cpu);
    bool is_gpu_sycl = (DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
            && eng.get_kind() == dnnl::engine::kind::gpu);
    if (is_cpu_sycl || is_gpu_sycl) {
#ifdef DNNL_USE_SYCL_BUFFERS
        auto buffer = mem.get_sycl_buffer<uint8_t>();
        auto dst = buffer.get_access<cl::sycl::access::mode::write>();
        uint8_t *dst_ptr = dst.get_pointer();
#elif defined(DNNL_USE_DPCPP_USM)
        uint8_t *dst_ptr = (uint8_t *)mem.get_data_handle();
#else
#error "Not expected"
#endif
        for (size_t i = 0; i < size; ++i)
            dst_ptr[i] = ((uint8_t *)handle)[i];
        return;
    }
#endif
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    if (eng.get_kind() == dnnl::engine::kind::gpu) {
        dnnl::stream s(eng);
        cl_command_queue q = s.get_ocl_command_queue();
        cl_mem m = mem.get_ocl_mem_object();

        cl_int ret = clEnqueueWriteBuffer(
                q, m, CL_TRUE, 0, size, handle, 0, NULL, NULL);
        if (ret != CL_SUCCESS)
            throw std::runtime_error("clEnqueueWriteBuffer failed");
        return;
    }
#endif

    if (eng.get_kind() == dnnl::engine::kind::cpu) {
        uint8_t *dst = static_cast<uint8_t *>(mem.get_data_handle());
        for (size_t i = 0; i < size; ++i)
            dst[i] = ((uint8_t *)handle)[i];
        return;
    }

    assert(!"not expected");
}

#endif
