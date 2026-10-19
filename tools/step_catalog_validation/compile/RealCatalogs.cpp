// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalogRecipes.h"
using namespace step_semantics;
using namespace step_semantics::catalog_build;
#if defined(STEP_CATALOG_UNKNOWN_PROFILE)
using Invalid = CompiledCatalog<ProfileRecipe<static_cast<ProfileKind>(-1)>>;
static_assert(Invalid::catalog.names.size() != 0);
#else
using Planar = CompiledCatalog<ProfileRecipe<ProfileKind::Ap203e2Planar>>;
using General = CompiledCatalog<ProfileRecipe<ProfileKind::Ap203e2GeneralSource>>;
using Ap214 = CompiledCatalog<ProfileRecipe<ProfileKind::Ap214e3Source>>;
using Mbx = CompiledCatalog<ProfileRecipe<ProfileKind::Ap214MbxSource>>;
using N916 = CompiledCatalog<ProfileRecipe<ProfileKind::Ap203N916Source>>;
static_assert(Planar::analysis.code == Error::None && General::analysis.code == Error::None);
static_assert(Ap214::analysis.code == Error::None && Mbx::analysis.code == Error::None);
static_assert(N916::analysis.code == Error::None && N916::catalog.names.size() == 137);
static_assert(Planar::catalog.names.size() && General::catalog.names.size() && Ap214::catalog.names.size() && Mbx::catalog.names.size());
#endif
