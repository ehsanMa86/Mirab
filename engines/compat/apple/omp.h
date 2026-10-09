/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    engines/compat/apple/omp.h
 * Purpose: Stand-in for <omp.h> on macOS, where Apple clang ships no OpenMP runtime.
 *
 * Copyright (C) 2026 Ehsan Madadi
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
//
// EPANET-MSX includes <omp.h> but uses only "#pragma omp" directives, no OpenMP
// functions. Mirab builds MSX without -fopenmp on every platform, so those
// directives are ignored and MSX runs single-threaded; an empty header is enough.
#ifndef MIRAB_COMPAT_OMP_H
#define MIRAB_COMPAT_OMP_H
#endif
