/***************************************************************************//**
 * @file
 * @brief CS result config.
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#ifndef CS_RESULT_CONFIG_H
#define CS_RESULT_CONFIG_H

// <<< Use Configuration Wizard in Context Menu >>>

// <h> Header of CS result configuration

// <e CS_RESULT_LOG> Enable result log
// <i> Enable CS result component logger
// <i> Default: 0
#ifndef CS_RESULT_LOG
#define CS_RESULT_LOG                              (0)
#endif

// <s CS_RESULT_LOG_PREFIX> Log prefix
// <i> Default: "[result]"
#ifndef CS_RESULT_LOG_PREFIX
#define CS_RESULT_LOG_PREFIX                       "[result]"
#endif

// </e>

// <o CS_RESULT_MAX_BUFFER_SIZE> Specify maximum size of result data <32..255>
// <i> Default: 32
#ifndef CS_RESULT_MAX_BUFFER_SIZE
#define CS_RESULT_MAX_BUFFER_SIZE                  (128)
#endif

// </h>

// <<< end of configuration section >>>

#endif // CS_RESULT_CONFIG_H
