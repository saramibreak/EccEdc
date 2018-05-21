// stdafx.h : 標準のシステム インクルード ファイルのインクルード ファイル、または
// 参照回数が多く、かつあまり変更されない、プロジェクト専用のインクルード ファイル
// を記述します。
//

#pragma once

#include "targetver.h"

// TODO: プログラムに必要な追加ヘッダーをここで参照してください。

#pragma warning(disable: 4710 4711)
#pragma warning(push)
#pragma warning(disable: 4365 4464 4571 4625 4626 4668 4774 4820 5026 5027 5039)
#include <Windows.h>

#include <string>
#include <cstdio>
#include <cassert>
#include <vector>

#include <Shlwapi.h>

#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

#pragma comment(lib, "shlwapi.lib")

#include <sstream>
#include <fstream>

#pragma warning(pop)
