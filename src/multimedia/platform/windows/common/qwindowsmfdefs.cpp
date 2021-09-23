/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwindowsmfdefs_p.h"

const GUID QMM_MFTranscodeContainerType_ADTS  = {0x132fd27d, 0x0f02, 0x43de, {0xa3, 0x01, 0x38, 0xfb, 0xbb, 0xb3, 0x83, 0x4e}};
const GUID QMM_MFTranscodeContainerType_ASF   = {0x430f6f6e, 0xb6bf, 0x4fc1, {0xa0, 0xbd, 0x9e, 0xe4, 0x6e, 0xee, 0x2a, 0xfb}};
const GUID QMM_MFTranscodeContainerType_AVI   = {0x7edfe8af, 0x402f, 0x4d76, {0xa3, 0x3c, 0x61, 0x9f, 0xd1, 0x57, 0xd0, 0xf1}};
const GUID QMM_MFTranscodeContainerType_FLAC  = {0x31344aa3, 0x05a9, 0x42b5, {0x90, 0x1b, 0x8e, 0x9d, 0x42, 0x57, 0xf7, 0x5e}};
const GUID QMM_MFTranscodeContainerType_MP3   = {0xe438b912, 0x83f1, 0x4de6, {0x9e, 0x3a, 0x9f, 0xfb, 0xc6, 0xdd, 0x24, 0xd1}};
const GUID QMM_MFTranscodeContainerType_MPEG4 = {0xdc6cd05d, 0xb9d0, 0x40ef, {0xbd, 0x35, 0xfa, 0x62, 0x2c, 0x1a, 0xb2, 0x8a}};
const GUID QMM_MFTranscodeContainerType_WAVE  = {0x64c3453c, 0x0f26, 0x4741, {0xbe, 0x63, 0x87, 0xbd, 0xf8, 0xbb, 0x93, 0x5b}};

const GUID QMM_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID = {0x8ac3587a, 0x4ae7, 0x42d8, {0x99, 0xe0, 0x0a, 0x60, 0x13, 0xee, 0xf9, 0x0f}};
const GUID QMM_MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID = {0x14dd9a1c, 0x7cff, 0x41be, {0xb1, 0xb9, 0xba, 0x1a, 0xc6, 0xec, 0xb5, 0x71}};
const GUID QMM_MF_TRANSCODE_CONTAINERTYPE = {0x150ff23f, 0x4abc, 0x478b, {0xac, 0x4f, 0xe1, 0x91, 0x6f, 0xba, 0x1c, 0xca}};

const GUID QMM_MF_SD_STREAM_NAME = {0x4f1b099d, 0xd314, 0x41e5, {0xa7, 0x81, 0x7f, 0xef, 0xaa, 0x4c, 0x50, 0x1f}};
const GUID QMM_MF_SD_LANGUAGE = {0xaf2180, 0xbdc2, 0x423c, {0xab, 0xca, 0xf5, 0x3, 0x59, 0x3b, 0xc1, 0x21}};

const GUID QMM_KSCATEGORY_VIDEO_CAMERA = {0xe5323777, 0xf976, 0x4f5b, {0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44}};

const GUID QMM_MR_POLICY_VOLUME_SERVICE = {0x1abaa2ac, 0x9d3b, 0x47c6, {0xab, 0x48, 0xc5, 0x95, 0x6, 0xde, 0x78, 0x4d}};

const PROPERTYKEY QMM_PKEY_Device_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

