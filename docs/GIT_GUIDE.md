# 日常 Git 管理

仓库根目录为`C:\Users\y\Desktop\c8t6`。STM、ESP和安卓都在同一个仓库，`stmc8t6`不再单独存放`.git`。远程为`https://github.com/yuanlairuqi/stmc8t6.git`。

## 提交与同步

```powershell
cd C:\Users\y\Desktop\c8t6
git status
git pull --ff-only
git switch -c feat/your-change
# 修改代码后检查差异并运行测试
git diff
python tools/test_all.py --cc 'D:\mingw\code\mingw64\bin\gcc.exe' --java-bin 'D:\uu\bin'
git add <本次相关的文件或目录>
git diff --cached
git commit -m 'feat: describe one concrete change'
git push -u origin HEAD
```

在GitHub发起PR，把功能分支合入main。单人日常小修改也可以在main提交后直接push；提交前仍需检查差异和测试。避免强推main；pull遇到分叉时先检查各自提交再合并，不直接重置本地代码。

提交建议：`feat`新增功能、`fix`修复、`test`测试、`docs`说明、`chore`工具或结构。每次提交聚焦一个可解释的变更。共享协议或peer文件修改时，同步更新ESP/STM两份文件与协议说明；根测试会拒绝不同步的文件。

## 版本

`v3.0.0`记录本次安卓/BLE/UART电机控制软件版本。BLE版本3与UART版本2是协议版本，不是三个项目分别独立的Git版本。

```powershell
git tag -a v3.0.1 -m 'Describe tested changes and hardware limits'
git push origin v3.0.1
```

标签发布后保持不变；后续修改增加新的版本。APK/固件在GitHub Release附件中发布，注明默认硬件配置、测试范围、SHA256。不要把签名私钥上传到仓库，也不要把本地完整备份压缩包直接用作公开源码包。

## 本地设置与忽略规则

本仓库设置`pull.ff=only`、`fetch.prune=true`、`push.default=simple`，不修改全局Git偏好。`.gitattributes`明确源码LF、PowerShell/批处理CRLF。

忽略build、机器IDE配置、备份、私钥、APK及固件等生成产物；验证目录中的日志仍可提交。旧版原始STM时钟/IOC校验的必要样本放在`stmc8t6/tests/fixtures/stm32_initial`，不依赖本地backup目录。手册、STEP和桌面截图仅保留本地。

仓库整理前的Git元数据和bundle备份保存在本次Codex工作目录；桌面的完整项目ZIP也保留。历史查询可用`git log --oneline --all --decorate`，路径追踪可用`git log --follow -- stmc8t6/Core/Src/main.c`。
