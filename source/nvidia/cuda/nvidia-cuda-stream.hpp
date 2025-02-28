/*
 * Modern effects for a modern Streamer
 * Copyright (C) 2020 Michael Fabian Dirks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once
#include <memory>
#include "nvidia-cuda.hpp"

namespace streamfx::nvidia::cuda {
	class stream {
		std::shared_ptr<::streamfx::nvidia::cuda::cuda> _cuda;
		::streamfx::nvidia::cuda::stream_t              _stream;

		public:
		~stream();
		stream(::streamfx::nvidia::cuda::stream_flags flags    = ::streamfx::nvidia::cuda::stream_flags::DEFAULT,
			   int32_t                                priority = 0);

		::streamfx::nvidia::cuda::stream_t get();

		void synchronize();
	};
} // namespace streamfx::nvidia::cuda
