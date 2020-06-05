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

DEV = {
   "priority"                   : "default",
   "bus type"                   : "pci",
   "device info"                : {
      "vendor list"                : [
         {
            "vendor id"               : "1924",
            "vendor description"      : "Solarflare",
            "devices" : [
               {
                  "device id"         : "0a03",
                  "sub vendor id"     : "1924",
                  "description"       : "SFC9220 10/40G Ethernet Controller",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "8022",
                  "description"       : "XtremeScale X2522 10G Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "8028",
                  "description"       : "XtremeScale X2522 25G Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "8027",
                  "description"       : "XtremeScale X2541 Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "802a",
                  "description"       : "XtremeScale X2542 Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "802c",
                  "description"       : "XtremeScale X2522 25G Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "8024",
                  "description"       : "XtremeScale X2562 10/25G OCP Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "802d",
                  "description"       : "XtremeScale X2562 10/25G OCP Network Adapter",
               },
               {
                  "device id"         : "0b03",
                  "sub vendor id"     : "1924",
                  "sub device id"     : "802b",
                  "description"       : "XtremeScale X2552 10/25G OCP Network Adapter",
               },

            ],
         },
      ]
   },
}
