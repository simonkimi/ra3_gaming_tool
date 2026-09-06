# C++ 地图查询接入

桌面端（Ra3 战区中枢）运行后，C++ 可通过本机 named pipe 发现 HTTP 端口，再查询地图信息与缩略图。客户端未启动时 pipe 不存在。查询**不会**把主窗口拉到前台。

## 1. 前置条件

- Windows 桌面端进程必须已在运行。
- 连接 `\\.\pipe\ra3-battlezone-hub` 失败（文件不存在 / 拒绝访问）即视为未启动。

## 2. 发现 HTTP 端口

Pipe 名称（固定）：

```text
\\.\pipe\ra3-battlezone-hub
```

1. `CreateFileW` 打开 pipe（`GENERIC_READ`，`OPEN_EXISTING`）。
2. 读取直到断开或读到换行。内容为 UTF-8 JSON。
3. 关闭句柄。

示例：

```json
{ "app": "ra3-battlezone-hub", "version": "4.4.0", "port": 17653 }
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `app` | string | 固定为 `ra3-battlezone-hub`，用来确认连到的是本客户端 |
| `version` | string | 客户端版本 |
| `port` | number | 本机 HTTP 端口，随后所有请求打到 `127.0.0.1:{port}` |

HTTP 实际端口是 `17653`、`28917`、`40179` 三者之一。不要写死端口，以 pipe 返回值为准。

建议超时：连接与读取各 1–2 秒即可。

## 3. 查询地图

```http
POST http://127.0.0.1:{port}/maps/lookup
Content-Type: application/json; charset=utf-8
```

Body（UTF-8）：

```json
{
  "path": "C:\\Users\\simon\\AppData\\Roaming\\Red Alert 3\\Maps\\解放战争正式版（6人）\\解放战争正式版（6人）.map"
}
```

`path` 可以是绝对路径、相对路径或仅地图名，例如：

- `C:\Users\...\Maps\解放战争正式版（6人）\解放战争正式版（6人）.map`
- `\data\maps\xxxxxxx\xxxxxxx.map`
- `/data/maps/xxxxxxx/xxxxxxx.map`
- `xxxxxxx.map`

客户端**只使用最后一段去掉 `.map` 后的名称**，盘符、前导 `\`、`data\maps` 等父目录一律忽略。随后在本客户端设置的地图目录（`app.maps.storage`）下按该名称查找本地文件。

JSON 里反斜杠按规范应写成 `\\`。若 C++ 把 `\data\maps\...` 直接拼进 JSON（未转义），客户端也会尽量按字面路径解析。使用正斜杠 `/data/maps/...` 可避免转义问题。

建议超时：10–20 秒。首次合成本地小地图或下载远程缩略图可能需要数秒。

## 4. 响应

一律 `application/json; charset=utf-8`。

### 4.1 地图未找到 — `200`

```json
{ "isFound": false }
```

此时不会编码或下载图片。

### 4.2 地图已找到 — `200`

```json
{
  "isFound": true,
  "info": {
    "id": 1294,
    "name": "解放战争正式版（6人）",
    "displayName": "解放战争正式版（6人）",
    "description": "游戏难度：★★★★",
    "user": {
      "uid": "iYYV1WssR3",
      "nickName": "塔防大佬来",
      "publicContact": ""
    },
    "author": "961730682",
    "playerCount": 6,
    "tags": ["PVE", "进攻"],
    "customTags": [],
    "location": [
      {
        "location": 1,
        "playerType": "any",
        "plotType": "any",
        "team": "any"
      }
    ],
    "statistic": {
      "downloadCount": 476,
      "favoriteCount": 10,
      "rating": null,
      "difficulty": null,
      "authorDifficulty": null,
      "playCount": 117,
      "uniquePlayerCount": 395
    },
    "size": 1018768,
    "thumbnailFileKey": "解放战争正式版（6人）/thumbnail.webp",
    "artFileKey": "解放战争正式版（6人）/preview.webp",
    "createdAt": "2021-12-21T16:00:00Z"
  },
  "imagePath": "E:\\...\\cache\\local-map-preview\\...\\....png"
}
```

`info` 与 `GET /v2/maps/precise/detail?name=` 命中时的 `info` 相同，包含 `description`、`tags`、`user` 等详情字段，具体以服务端为准。

`imagePath`：

| 值 | 含义 |
|---|---|
| `""` | 无图（合成失败或远程下载失败） |
| 以 `.png` 结尾 | 本地已有原图，按本机地图页同一套逻辑合成/命中 PNG 缓存 |
| 以 `.webp` 结尾 | 本地没有原图，已按客户端分流下载远程缩略图 |

C++ 按扩展名加载即可。路径是本机绝对路径，可直接 `CreateFile` / 解码。

### 4.3 错误

| HTTP | Body | 含义 |
|---|---|---|
| `400` | `{"ok":false}` | `path` 缺失或无法解析出合法地图名 |
| `405` | `{"ok":false}` | 方法不是 POST |
| `502` | `{"ok":false}` | 精确查询 API 网络失败 |
| `500` | `{"ok":false}` | 服务内部错误 |

`400` / `502` / `500` **不是**「地图不存在」。未找到只用 `200` + `isFound: false`。

## 5. 推荐流程

```text
CreateFile(\\.\pipe\ra3-battlezone-hub)
  -> 读 JSON 得到 port
  -> POST http://127.0.0.1:{port}/maps/lookup  {"path":"..."}
  -> isFound == false 则结束
  -> isFound == true 使用 info，并按 imagePath 扩展名加载 png/webp
```

不要依赖桌面窗口被激活。不要轮询唤醒接口。

## 6. 查询评论

地图查询成功后，用 `info.id` 拉取该图评论。客户端会请求 `GET /v2/maps/{id}/comments`，并把 HTTP 状态码与 JSON 原样转给 overlay。

```http
GET http://127.0.0.1:{port}/maps/{id}/comments
```

例如 `GET /maps/1294/comments`。`id` 必须是正整数。

建议超时：10–15 秒。

### 6.1 成功 — 与远端评论接口相同

远端返回什么，本地就回什么。通常是 `200` 加评论数组：

```json
[
  {
    "id": 1,
    "user": {
      "uid": "iYYV1WssR3",
      "nickName": "塔防大佬来",
      "publicContact": ""
    },
    "content": "很好玩",
    "createdAt": "2024-01-01T00:00:00Z",
    "isCollapsed": false,
    "replyToId": null,
    "rating": 5,
    "difficulty": null
  }
]
```

没有评论时是空数组 `[]`。字段以服务端为准。

### 6.2 错误

| HTTP | Body | 含义 |
|---|---|---|
| `405` | `{"ok":false}` | 方法不是 GET |
| `502` | `{"ok":false}` | 评论 API 网络失败 |
| 其它 | 远端原文 | 远端非网络错误（如 404）会原样转发状态码和 body |

无效路径（缺 id、id 为 0、非数字）不会命中本接口，返回 `404`。

## 7. 可选回退：探测 HTTP 端口

浏览器侧仍使用 `GET /ping`。C++ 在 pipe 不可用时也可依次请求：

```text
GET http://127.0.0.1:17653/ping
GET http://127.0.0.1:28917/ping
GET http://127.0.0.1:40179/ping
```

命中时返回：

```json
{ "app": "ra3-battlezone-hub", "version": "4.4.0" }
```

`app` 必须是 `ra3-battlezone-hub`。优先使用 named pipe，不必每次扫三个端口。

## 8. Win32 端口发现示例

```cpp
#include <windows.h>
#include <string>

static const wchar_t kPipeName[] = L"\\\\.\\pipe\\ra3-battlezone-hub";

bool ReadHubPort(unsigned short& port, std::string& json) {
  HANDLE pipe = CreateFileW(
      kPipeName,
      GENERIC_READ,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return false;
  }

  char buffer[1024];
  DWORD read = 0;
  json.clear();
  while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    json.append(buffer, read);
  }
  CloseHandle(pipe);

  // 解析 json 中的 "port": <number>
  auto pos = json.find("\"port\"");
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find(':', pos);
  if (pos == std::string::npos) {
    return false;
  }
  port = static_cast<unsigned short>(std::stoi(json.substr(pos + 1)));
  return port != 0;
}
```

查询请求请用任意 HTTP 客户端向 `127.0.0.1` 发 UTF-8 JSON。`path` 中的中文不要改成系统 ANSI 代码页。
