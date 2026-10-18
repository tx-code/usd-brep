// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"
const step_semantics::Catalog& LinkedCatalog();
int main()
{
    const auto& catalog = LinkedCatalog();
    return catalog.names.size() == 1 && catalog.names[0] == "A" && catalog.declarations[0].fields[0].name == "x" ? 0 : 1;
}
