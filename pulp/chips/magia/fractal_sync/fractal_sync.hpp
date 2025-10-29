/*
 * Copyright (C) 2025 Fondazione Chips-IT
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
 */

/*
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#pragma once

#include <stdint.h>

template<typename T>
//class SlvPortInput
class PortReq
{
public:
    bool sync;
    T aggr;
    T id_req;
};

template<typename T>
//class SlvPortOutput
class PortResp
{
public:
    bool wake;
    T lvl;
    T id_rsp;
    bool error;
};
