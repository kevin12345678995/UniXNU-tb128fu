# 4 GB / 64 GB TB128FU 分支（fork 说明）

这个分支把作者面向 **6 GB + 128 GB Samsung UFS** 样机的 tb128fu 端口，改成能在一台
**4 GB + 64 GB Micron UFS** 的 TB128FU 上启动。硬件其余部分（SM6225/KHAJE、BOE `hx83102p` 屏、
Himax 触摸、`ufshc@4804000`、96 MiB boot_a、`dtb_idx=8`/`dtbo_idx=12`、ZUI 15.0.619 vendor）与本机实测一致。

## 改了什么

| 文件 | 改动 | 原因 |
| --- | --- | --- |
| `board.toml` | `[userdata] last_lba 0x1c457fa → 0xd5fffa` | 本机 userdata = 44.95 GiB（4K-LBA `0x222f31..0xd5fffa`），作者是 104.5 GiB |
| `board.toml` | `[payload] dram_size 0x180000000 → 0x100000000` | 本机 `/memory` 上界 0x140000000 |
| `board.toml` | `[payload.adt] owned_memory_size 0x100000000 → 0x80000000` | 上段空闲 DRAM 只有 3 GiB，4 GiB 窗口不存在，DarwinBoot 会 `NO WINDOW` |
| `tb128fu.dtb` | 由本机 `/sys/firmware/fdt` 重新生成（删 `/chosen/{bootargs,linux,initrd-*}`） | 作者提供的是他机器的合并树 |
| `patches/MemoryMapLib.c.patch` | 低段 `0x1A400000`、`Ramdisk 72 MiB`、上段 `0x85800000+0xBA800000` | 固件的内存描述符是编译期常量，6 GB 机的表在本机会漏掉 17 MiB 并越界 |
| `patches/GccUfs.h.patch` | `UFS_USERDATA_LAST 0x1c457fa → 0xd5fffa` | UFS 驱动里硬编码了 userdata 窗口 |

注意 `.gitignore` 原本忽略 `*.dtb`，本分支加了 `!tb128fu.dtb` 例外，否则设备树不会被提交。

## 推送并运行 CI

```bash
# 1) 在你的机器上（本分支已就绪，只需换 remote）
cd UniXNU-tb128fu
git checkout -b 4g64g
git add -A
git commit -m "tb128fu 4GB/64GB: userdata geometry, 2 GiB DarwinBoot window, patched MemoryMapLib, own device tree, CI"
git remote add mine https://github.com/<你的账号>/UniXNU-tb128fu.git
git push -u mine 4g64g
```

推送后 Actions 会自动跑 `build-tb128fu-4g64g`（`on.push` 覆盖 `4g64g` 分支）。也可以在 Actions 页面手动 Run workflow。

## 预期结果

- 用时约 60–90 分钟，其中「Build the Linux Mach-O toolchain」一步占大头。
- 关键校验点在「Apply the 4 GB / 64 GB changes」的 diff 输出里，应能看到：
  - `MemoryMapLib.c`: 6 行改动（3 增 3 删）
  - `GccUfs.h`: 1 行改动
- `Build package and firmware` 会先跑 `scripts/check-tb128fu.py`，日志应出现：
  `54 descriptors, 0 vendor inputs` 与 splash 区落在 `Display Reserved` 内。
- 产物 `tb128fu-4g64g-boot` 里有 `boot.img`（约 57–72 MiB）与 `check.json`。

## 拿到 boot.img 之后

```powershell
fastboot --set-active=a
fastboot flash boot_a boot.img
fastboot reboot
```

回滚用本机 `backup/tb128fu-20261116/boot_a.img`（96 MiB 原厂镜像，md5 `43c130fe…12f87f0`）。

## 可选：Stage 4/5（XNU 内核）

`payload-macos` 作业是手动触发，需要仓库 secret `UNIXNU_UNIQUE_CHIP_ID`（本机的平台种子，
已记录在交付包的 `adt/platform-seed.txt`：`7f3a91c4b6d05e28`）。它会下载 7 GB IPSW 并连编两次内核。

## 提醒

即使全部成功，这台 4 GB 机器得到的是 2 GiB 窗口的 iPadOS（等价作者 `tb128fu-007` 阶段）：
能到 SpringBoard、空闲内存约 5 MiB，且无 Wi-Fi/音频/电池/按键/休眠，420 秒自动重启回 fastboot。