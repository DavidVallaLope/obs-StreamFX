// Copyright (c) 2020 Michael Fabian Dirks <info@xaymar.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "nvidia-vfx.hpp"
#include <filesystem>
#include <mutex>
#include "nvidia/cuda/nvidia-cuda-obs.hpp"
#include "obs/gs/gs-helper.hpp"
#include "util/util-logging.hpp"
#include "util/util-platform.hpp"

#ifdef _DEBUG
#define ST_PREFIX "<%s> "
#define D_LOG_ERROR(x, ...) P_LOG_ERROR(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#define D_LOG_WARNING(x, ...) P_LOG_WARN(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#define D_LOG_INFO(x, ...) P_LOG_INFO(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#define D_LOG_DEBUG(x, ...) P_LOG_DEBUG(ST_PREFIX##x, __FUNCTION_SIG__, __VA_ARGS__)
#else
#define ST_PREFIX "<nvidia::vfx::vfx> "
#define D_LOG_ERROR(...) P_LOG_ERROR(ST_PREFIX __VA_ARGS__)
#define D_LOG_WARNING(...) P_LOG_WARN(ST_PREFIX __VA_ARGS__)
#define D_LOG_INFO(...) P_LOG_INFO(ST_PREFIX __VA_ARGS__)
#define D_LOG_DEBUG(...) P_LOG_DEBUG(ST_PREFIX __VA_ARGS__)
#endif

#if defined(WIN32)
#include <KnownFolders.h>
#include <ShlObj.h>
#include <Windows.h>

#define LIB_NAME "NVVideoEffects.dll"
#else
#define LIB_NAME "libNVVideoEffects.so"
#endif

#define ST_ENV_NVIDIA_VIDEO_EFFECTS_SDK_PATH L"NV_VIDEO_EFFECTS_PATH"

#define NVVFX_LOAD_SYMBOL(NAME)                                                          \
	{                                                                                    \
		NAME = reinterpret_cast<decltype(NAME)>(_library->load_symbol(#NAME));           \
		if (!NAME)                                                                       \
			throw std::runtime_error("Failed to load '" #NAME "' from '" LIB_NAME "'."); \
	}

streamfx::nvidia::vfx::vfx::~vfx()
{
	D_LOG_DEBUG("Finalizing... (Addr: 0x%" PRIuPTR ")", this);

#ifdef WIN32
	// Remove the DLL directory from the library loader paths.
	if (_extra != nullptr) {
		RemoveDllDirectory(reinterpret_cast<DLL_DIRECTORY_COOKIE>(_extra));
	}
#endif

	{ // The library may need to release Graphics and CUDA resources.
		auto gctx = ::streamfx::obs::gs::context();
		auto cctx = ::streamfx::nvidia::cuda::obs::get()->get_context()->enter();
		_library.reset();
	}
}

streamfx::nvidia::vfx::vfx::vfx()
{
	std::filesystem::path sdk_path;
	auto                  gctx = ::streamfx::obs::gs::context();
	auto                  cctx = ::streamfx::nvidia::cuda::obs::get()->get_context()->enter();

	D_LOG_DEBUG("Initializing... (Addr: 0x%" PRIuPTR ")", this);

	// Figure out the location of the Video Effects SDK, if it is installed.
#ifdef WIN32
	{
		DWORD                env_size;
		std::vector<wchar_t> buffer;

		env_size = GetEnvironmentVariableW(ST_ENV_NVIDIA_VIDEO_EFFECTS_SDK_PATH, nullptr, 0);
		if (env_size > 0) {
			buffer.resize(static_cast<size_t>(env_size) + 1);
			env_size = GetEnvironmentVariableW(ST_ENV_NVIDIA_VIDEO_EFFECTS_SDK_PATH, buffer.data(),
											   static_cast<DWORD>(buffer.size()));
			sdk_path = std::wstring(buffer.data(), buffer.size());
		} else {
			PWSTR   str = nullptr;
			HRESULT res = SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &str);
			if (res == S_OK) {
				sdk_path = std::wstring(str);
				CoTaskMemFree(str);
				sdk_path /= "NVIDIA Corporation";
				sdk_path /= "NVIDIA Video Effects";
			}
		}
	}
#else
	throw std::runtime_error("Not yet implemented.");
#endif

	// Check if any of the found paths are valid.
	if (!std::filesystem::exists(sdk_path)) {
		D_LOG_ERROR("No supported NVIDIA SDK is installed to provide '%s'.", LIB_NAME);
		throw std::runtime_error("Failed to load '" LIB_NAME "'.");
	}

	// Try and load the library.
	{
#ifdef WIN32
		// On platforms where it is possible, modify the linker directories.
		DLL_DIRECTORY_COOKIE ck = AddDllDirectory(sdk_path.wstring().c_str());
		_extra                  = reinterpret_cast<void*>(ck);
		if (ck == 0) {
			DWORD       ec = GetLastError();
			std::string error;
			{
				LPWSTR str;
				FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER
								   | FORMAT_MESSAGE_IGNORE_INSERTS,
							   nullptr, ec, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
							   reinterpret_cast<LPWSTR>(&str), 0, nullptr);
				error = ::streamfx::util::platform::native_to_utf8(std::wstring(str));
				LocalFree(str);
			}
			D_LOG_WARNING("Failed to add '%'s to the library loader paths with error: %s (Code %" PRIu32 ")",
						  sdk_path.string().c_str(), error.c_str(), ec);
		}
#endif

		std::filesystem::path paths[] = {
			LIB_NAME,
			util::platform::native_to_utf8(std::filesystem::path(sdk_path) / LIB_NAME),
		};

		for (auto path : paths) {
			try {
				_library = ::streamfx::util::library::load(path);
			} catch (std::exception const& ex) {
				D_LOG_ERROR("Failed to load '%s' with error: %s", path.string().c_str(), ex.what());
			} catch (...) {
				D_LOG_ERROR("Failed to load '%s'.", path.string().c_str());
			}

			if (_library) {
				break;
			}
		}

		if (!_library) {
#ifdef WIN32
			// Remove the DLL directory from the library loader paths.
			if (_extra != nullptr) {
				RemoveDllDirectory(reinterpret_cast<DLL_DIRECTORY_COOKIE>(_extra));
			}
#endif
			throw std::runtime_error("Failed to load " LIB_NAME ".");
		}
	}

	// Store the model path for later use.
	_model_path = std::filesystem::path(sdk_path) / "models";

	{ // Load Symbols
		NVVFX_LOAD_SYMBOL(NvVFX_GetVersion);
		NVVFX_LOAD_SYMBOL(NvVFX_CreateEffect);
		NVVFX_LOAD_SYMBOL(NvVFX_DestroyEffect);
		NVVFX_LOAD_SYMBOL(NvVFX_SetU32);
		NVVFX_LOAD_SYMBOL(NvVFX_SetS32);
		NVVFX_LOAD_SYMBOL(NvVFX_SetF32);
		NVVFX_LOAD_SYMBOL(NvVFX_SetF64);
		NVVFX_LOAD_SYMBOL(NvVFX_SetU64);
		NVVFX_LOAD_SYMBOL(NvVFX_SetImage);
		NVVFX_LOAD_SYMBOL(NvVFX_SetObject);
		NVVFX_LOAD_SYMBOL(NvVFX_SetString);
		NVVFX_LOAD_SYMBOL(NvVFX_SetCudaStream);
		NVVFX_LOAD_SYMBOL(NvVFX_GetU32);
		NVVFX_LOAD_SYMBOL(NvVFX_GetS32);
		NVVFX_LOAD_SYMBOL(NvVFX_GetF32);
		NVVFX_LOAD_SYMBOL(NvVFX_GetF64);
		NVVFX_LOAD_SYMBOL(NvVFX_GetU64);
		NVVFX_LOAD_SYMBOL(NvVFX_GetImage);
		NVVFX_LOAD_SYMBOL(NvVFX_GetObject);
		NVVFX_LOAD_SYMBOL(NvVFX_GetString);
		NVVFX_LOAD_SYMBOL(NvVFX_GetCudaStream);
		NVVFX_LOAD_SYMBOL(NvVFX_Run);
		NVVFX_LOAD_SYMBOL(NvVFX_Load);
	}

	{ // Assign proper GPU.
		auto cctx = ::streamfx::nvidia::cuda::obs::get()->get_context()->enter();
		NvVFX_SetU32(nullptr, PARAMETER_GPU, 0);
	}
}

std::shared_ptr<::streamfx::nvidia::vfx::vfx> streamfx::nvidia::vfx::vfx::get()
{
	static std::weak_ptr<streamfx::nvidia::vfx::vfx> instance;
	static std::mutex                                lock;

	std::unique_lock<std::mutex> ul(lock);
	if (instance.expired()) {
		auto hard_instance = std::make_shared<streamfx::nvidia::vfx::vfx>();
		instance           = hard_instance;
		return hard_instance;
	}
	return instance.lock();
}

std::filesystem::path const& streamfx::nvidia::vfx::vfx::model_path()
{
	return _model_path;
}
