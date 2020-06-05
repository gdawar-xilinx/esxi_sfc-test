# Copyright (c) 2017-2020 Xilinx, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Check NDDK Version
PWD=$(pwd)

# Retrieve NDDK directory name
NDDK_DIR=$(echo ${PWD} | sed 's/\//\ /g' | awk '{print $3}')
SEPARATOR=-

get_len_upto_separator () {
  local idx=0
  local keyword

  keyword=${NDDK_DIR:$idx:1}
  while [ "$SEPARATOR" != "$keyword" ]
  do
    let "idx += 1"
    keyword=${NDDK_DIR:$idx:1}
  done

  let "idx += 1"
  return $idx
}

get_ndk_ver () {
  local nddk_ver
  local start=0
  local end=0

  get_len_upto_separator
  start=$?
  let "start += 1"
  end=`expr $start + 2`

  nddk_ver=$(echo $NDDK_DIR | cut -c $start-$end)
  echo ${nddk_ver} | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//'
}

get_ndk_ver
