/*
 * WebP image format definitions
 * Copyright (c) 2020 Pexeso Inc.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * WebP image format definitions.
 */

#ifndef AVCODEC_WEBP_H
#define AVCODEC_WEBP_H

#define VP8X_FLAG_ANIMATION             0x02
#define VP8X_FLAG_XMP_METADATA          0x04
#define VP8X_FLAG_EXIF_METADATA         0x08
#define VP8X_FLAG_ALPHA                 0x10
#define VP8X_FLAG_ICC                   0x20

#define ANMF_DISPOSAL_METHOD            0x01
#define ANMF_DISPOSAL_METHOD_UNCHANGED  0x00
#define ANMF_DISPOSAL_METHOD_BACKGROUND 0x01

#define ANMF_BLENDING_METHOD            0x02
#define ANMF_BLENDING_METHOD_ALPHA      0x00
#define ANMF_BLENDING_METHOD_OVERWRITE  0x02

#endif /* AVCODEC_WEBP_H */