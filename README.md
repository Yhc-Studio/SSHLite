# SSHLite

**SSHLite** 是一个面向 Windows 10 / 11 的轻量级原生 Win32 SSH 会话与端口转发管理器。

程序直接调用 Windows 自带的 **OpenSSH Client (`ssh.exe`)** 完成 SSH 连接、认证和转发，因此不需要 vcpkg、libssh2、OpenSSL SDK、CMake、.NET、WinUI 或 Windows App SDK。

> 当前正式版本：**1.0.0**  
> 中文说明在前，English documentation follows below.

---

## 主要特性

- 原生 **Win32 + Modern UI**
- 全界面使用 **Microsoft YaHei UI（微软雅黑 UI）**
- 中文 / English 实时切换
- 多主题与颜色风格：
  - 浅色 Light（默认）
  - 深色 Dark
  - 海洋蓝 Ocean
  - 翡翠绿 Emerald
  - 玫瑰 Rose
  - 自定义强调色 Custom Accent
- 独立功能页面：
  - 快速连接
  - 服务器历史
  - 隧道收藏
- 支持多个 SSH 会话同时运行
- 同一会话支持多个 `-L` 本地端口转发
- 支持不同服务器、不同 SSH 端口并行连接
- 密码认证
- SSH 私钥认证与私钥口令
- 完整解析本地转发映射：`本地监听 → 远端目标`
- 多个本地转发入口可选择并在浏览器打开
- 服务器历史自动记录
- 隧道收藏持久化
- 可选保存收藏的**明文 SSH 密码**
- 关闭程序、全部断开、删除记录等高影响操作均提供确认提示
- 支持 `Ctrl+A` 全选以及原生剪贴板快捷键
- 日志与历史数据有容量限制，避免长期运行持续增长
- 窗口大小、语言、主题与自定义强调色自动保存

---

## 快速开始

### 1. 建立一个本地端口转发

例如：

```bat
ssh -CN -L 6006:127.0.0.1:6006 root@example.com -p 22
```

连接成功后，可通过：

```text
http://127.0.0.1:6006
```

访问远端服务。

### 2. 一个 SSH 会话转发多个端口

```bat
ssh -CN ^
  -L 6006:127.0.0.1:6006 ^
  -L 8800:127.0.0.1:8800 ^
  root@example.com -p 22
```

SSHLite 会分别识别：

```text
127.0.0.1:6006 → 127.0.0.1:6006
127.0.0.1:8800 → 127.0.0.1:8800
```

并提供对应的浏览器入口。

### 3. 转发到 SSH 服务器侧的其他主机

```bat
ssh -L 8888:10.0.0.25:8888 -p 6000 user@example.com
```

SSHLite 会区分三个不同概念：

```text
SSH 服务器： user@example.com:6000
本地监听：   127.0.0.1:8888
远端目标：   10.0.0.25:8888
浏览器入口： http://127.0.0.1:8888
```

### 4. 同时连接多个服务器

每次点击“新建连接”都会创建独立的 `ssh.exe` 进程，因此可以同时保持多个服务器和多个转发会话。

例如：

```text
#1  server-a:22     本地 :6006, :8800
#2  server-b:22022  本地 :8080
#3  server-c:22     SSH Keepalive
```

---

## 普通 SSH / 端口 22

下面这种命令也可以输入：

```bat
ssh user@example.com -p 22
```

SSHLite 的定位是 **SSH 连接与隧道管理器**，不是交互式终端模拟器。

对于没有远程命令、也没有端口转发的普通 SSH 连接，SSHLite 会提示后将其作为非交互 Keepalive 会话保持连接。

如果需要执行：

```text
ls
vim
top
bash
```

请使用 Windows Terminal、PowerShell 或其他终端程序中的 `ssh`。

---

## 支持的 OpenSSH 参数

SSHLite 将 SSH 连接交给 Windows `ssh.exe`，因此可以继续使用常见 OpenSSH 参数，例如：

```text
-C  -N  -g  -L  -R  -D  -J  -F  -o  -p  -i
```

部分云平台会给出：

```bat
ssh user@example.com -p 38793
```

SSHLite 会在启动 OpenSSH 前兼容并规范化这类参数顺序。

---

## 服务器历史

“服务器历史”页面用于查看最近连接过的 SSH 配置。

- 成功发起连接后自动记录
- 最多保留 20 条
- 可以重新载入到快速连接
- 可以单独删除或清空
- **不会保存 SSH 密码**
- **不会保存私钥口令**

---

## 隧道收藏

“隧道收藏”用于保存需要长期重复使用的连接配置，包括：

- 收藏名称
- SSH 命令
- 认证方式
- 私钥路径
- 是否自动打开浏览器
- 可选的 SSH 密码

### 关于明文密码

如果在密码认证收藏中填写密码，SSHLite 会按照用户选择将其以**明文**写入：

```text
SSHLite.ini
```

例如：

```ini
[Favorite.0]
PasswordPlain=example-password
```

这不是加密存储，也不使用 Windows Credential Manager。

因此：

- 不要把 `SSHLite.ini` 上传到 GitHub
- 不要将该文件同步到公开网盘
- 不要将包含密码的配置文件发送给其他人
- 不希望保存密码时，只需保持收藏密码字段为空

仓库的 `.gitignore` 已默认忽略 `SSHLite.ini`。

私钥口令始终不会持久化保存。

---

## 安全与操作边界

SSHLite 会对常见高风险或易误操作情况进行检查和提示：

- 本地 `-L` 端口已经被另一个 SSHLite 会话占用：阻止连接
- 本地端口已被其他程序监听：阻止连接
- 同一命令重复定义相同本地端口：阻止连接
- 完全相同的 SSH 会话已经运行：要求确认
- 使用 `-g` 或公开绑定地址：提示局域网暴露风险
- 没有 `-L` 的纯 SSH 会话：提示其为非交互 Keepalive
- “全部断开”：要求确认
- 删除历史、收藏或清空历史：要求确认
- 点击窗口关闭按钮或使用 `Alt+F4`：要求确认
- 如果仍有活动会话，退出时会明确提示所有 SSH 连接和端口转发都将被终止

程序使用：

```text
-o StrictHostKeyChecking=accept-new
```

新主机密钥可以被接受并写入 OpenSSH 的 `known_hosts`；已记录主机的密钥发生变化时仍由 OpenSSH 执行安全校验。

---

## 密码认证实现

密码认证使用 Windows OpenSSH 支持的：

```text
SSH_ASKPASS
SSH_ASKPASS_REQUIRE=force
```

主程序 `SSHLite.exe` 与辅助程序 `SSHLiteAskPass.exe` 之间通过每个会话独立的本地 Named Pipe 临时传递认证信息。

除明确选择保存到“隧道收藏”的明文密码外，运行时密码和私钥口令不会被写入临时文本文件。

---

## 界面与快捷键

支持：

```text
Ctrl+A    全选
Ctrl+C    复制
Ctrl+X    剪切
Ctrl+V    粘贴
```

`Ctrl+A` 可用于 SSH 命令、密码、私钥路径、私钥口令、收藏名称、收藏密码以及活动日志。

正式版首次启动时 SSH 命令输入框为空，不包含任何预设服务器配置。

---

## 系统要求

- Windows 10 / Windows 11
- x64
- Windows OpenSSH Client

程序会自动查找：

```text
ssh.exe
```

以及：

```text
%WINDIR%\System32\OpenSSH\ssh.exe
```

如果系统尚未安装 OpenSSH Client，请在 Windows“可选功能”中安装 **OpenSSH Client**。

---

## 从源码编译

### 开发环境

- Visual Studio 2022
- Desktop development with C++
- Windows 10 / 11 SDK

不需要：

```text
vcpkg
CMake
libssh2
OpenSSL SDK
.NET
Windows App SDK
```

### 编译步骤

打开：

```text
SSHLiteWin32.sln
```

选择：

```text
Release | x64
```

然后执行：

```text
生成 → 生成解决方案
```

输出目录：

```text
bin\Release\
├─ SSHLite.exe
└─ SSHLiteAskPass.exe
```

两个 EXE 必须位于同一个目录。

也可以运行：

```text
scripts\build_release.cmd
```

源码和 VS 工程均使用 UTF-8 配置，避免非英文 Windows 代码页造成中文字符串编译错误。

---

## 项目结构

```text
SSHLite-Win32/
├─ src/
│  ├─ main.cpp
│  ├─ MainWindow.cpp
│  ├─ MainWindow.h
│  ├─ SshBackend.cpp
│  ├─ SshBackend.h
│  ├─ SshCommand.cpp
│  ├─ SshCommand.h
│  ├─ AppIds.h
│  ├─ AppTypes.h
│  └─ AppServices.h
│
├─ askpass/
│  └─ askpass.cpp
│
├─ docs/
│  ├─ USAGE.md
│  └─ ARCHITECTURE.md
│
├─ scripts/
│  ├─ build_release.cmd
│  └─ pack_release.ps1
│
├─ .github/
│  ├─ workflows/
│  └─ ISSUE_TEMPLATE/
│
├─ SSHLiteWin32.sln
├─ SSHLiteWin32.vcxproj
├─ SSHLiteAskPass.vcxproj
├─ README.md
├─ CHANGELOG.md
├─ RELEASE_NOTES.md
├─ SECURITY.md
└─ LICENSE
```

### 模块职责

- `main.cpp`：最小 Windows 程序入口
- `MainWindow.*`：Modern Win32 UI、主题、语言、页面与交互
- `SshBackend.*`：`ssh.exe` 进程生命周期、AskPass、Named Pipe、多会话管理
- `SshCommand.*`：SSH 命令解析、`-L` 映射、端口与目标解析
- `AppTypes.h`：公共数据结构
- `AppIds.h`：版本号、控件 ID、消息 ID
- `AppServices.h`：UI 与 SSH 后端之间的轻量接口
- `askpass.cpp`：OpenSSH AskPass 辅助程序

---

## GitHub Actions 与发布

仓库包含 Windows x64 自动构建与 Release Workflow。

普通 Push / Pull Request 会构建 Release x64 并上传 Artifact。

发布 **1.0.0**：

```bash
git tag v1.0.0
git push origin v1.0.0
```

Release Workflow 会生成：

```text
SSHLite-v1.0.0-win-x64.zip
```

发布包包含：

```text
SSHLite.exe
SSHLiteAskPass.exe
README.md
RELEASE_NOTES.md
LICENSE
```

---

## 已知边界

SSHLite 目前不提供：

- 内嵌交互式 SSH Terminal
- SFTP 文件管理器
- Windows Credential Manager 凭据存储
- 内置 SSH 协议栈

这些功能不影响其作为轻量 SSH 会话与端口转发管理器使用。

---

## License

SSHLite 使用 **MIT License**。详见 [LICENSE](LICENSE)。

---

# English

## Overview

**SSHLite 1.0.0** is a lightweight native Win32 SSH session and local-forwarding manager for Windows 10/11.

It delegates SSH transport and cryptography to the Windows OpenSSH Client (`ssh.exe`), keeping the application small and avoiding third-party SSH SDK/runtime dependencies.

### Highlights

- Native Win32 modern UI
- Chinese / English live switching
- Microsoft YaHei UI typography
- Light, Dark, Ocean, Emerald, Rose and Custom Accent themes
- Multiple simultaneous SSH sessions
- Multiple `-L` forwards per session
- Password and private-key authentication
- Complete local-forward mapping display
- Dedicated Server History and Tunnel Favorites pages
- Optional plaintext password storage for Tunnel Favorites
- Close/disconnect/delete confirmation safeguards
- `Ctrl+A` Select All support in text fields and Activity log
- Windows OpenSSH backend with no vcpkg/libssh2 dependency

### Build

Open `SSHLiteWin32.sln` in Visual Studio 2022, select `Release | x64`, and build the solution.

Output:

```text
bin\Release\
├─ SSHLite.exe
└─ SSHLiteAskPass.exe
```

Keep both executable files in the same directory.

### Security note

Tunnel Favorites may optionally store an SSH password as plaintext in `SSHLite.ini`. Do not publish or share that file. Server History does not store passwords, and private-key passphrases are never persisted.

### License

MIT License.
