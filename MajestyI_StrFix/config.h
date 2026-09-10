#pragma once

// MajestyI_StrFix.dll —— ASI 插件配置
// 说明：本文件是「唯一配置源」。修改后重启游戏生效。

// 日志级别：0=关闭 1=仅摘要/命中 2=命中+详情(默认) 3=全量(含未命中/每次赋值)
#define LOG_LEVEL   2

// 是否把「未命中(纯 ASCII)」也记进日志的计数里（不写逐条明细，只累计数字）
#define LOG_COUNT_MISS      1

// 是否 hook sub_627AD0(宽串赋值) 做只读观测（不改变行为）
#define HOOK_WIDE_ASSIGN_OBSERVE   0

// 是否在转发原函数前，把待转换的 ANSI 源串打 dump（含十六进制）
#define LOG_DUMP_SOURCE     1

// 单条日志最多 dump 多少字节源串
#define LOG_DUMP_MAX_BYTES  64

// 日志文件名（相对游戏根目录/ASI 所在目录）
#define LOG_FILE_NAME   "MajestyI_StrFix.log"
