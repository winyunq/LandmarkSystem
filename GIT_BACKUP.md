# 本任务源码备份清单

保存地图开局单位接入既有国家坦克外观选择入口的程序修改。

| 仓库相对文件路径 | 用途 | 必须备份的原因 |
|---|---|---|
| `.gitignore` | 默认忽略新增文件 | 防止缓存及结果自动入库 |
| `GIT_BACKUP.md` | 逐项备份清单 | 明确程序恢复范围 |
| `Source/LandmarkSystem/Private/MassUnitInHere.cpp` | 开局通用坦克选择国家外观 | 复用 RTSInputSystem 的 ResolveCountryTankConfig；14,202 字节 |

不新增跟踪美术、生成结果、Binaries、Intermediate、Saved 和第三方依赖。依赖 RTSInputSystem 现有公共声明与实现、主项目单位协议和坦克美术资源；这些外部资产不包含在本源码提交中。本地 Git 提交不等于独立磁盘备份。
