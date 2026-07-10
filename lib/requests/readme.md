# requests — Thu vien HTTP cho Manios

## Nhap

```mno
inp requests
```

## Cach su dung co ban

### GET — Lay du lieu tu URL

```mno
inp requests

hold r = fetch("https://httpbin.org/get")
yell r["status_code"]
yell r["text"]
yell r["ok"]
```

### POST — Gui du lieu len server

```mno
hold r = fetch_with_opts("https://httpbin.org/post", vault {
    method: "POST",
    data: "name=manios&version=1.0"
})
yell r["status_code"]
yell r["text"]
```

### POST JSON

```mno
hold r = transmit_json("https://httpbin.org/post", vault {
    name: "manios",
    version: "1.0"
})
hold data = response_json(r)
yell data["json"]
```

### POST Form

```mno
hold r = transmit_form("https://httpbin.org/post", vault {
    user: "admin",
    pass: "secret"
})
yell r["text"]
```

### PUT

```mno
hold r = put_data("https://httpbin.org/put", "updated_data=1")
yell r["status_code"]
```

### PATCH

```mno
hold r = patch_data("https://httpbin.org/patch", "field=new_value")
yell r["status_code"]
```

### DELETE

```mno
hold r = remove_url("https://httpbin.org/delete")
yell r["status_code"]
```

### HEAD

```mno
hold r = head_only("https://httpbin.org/get")
yell r["status_code"]
yell r["headers"]
```

## Cach lay du lieu tu response

Mo response la mot vault chua cac truong:

| Truong | Kieu | Mo ta |
|--------|------|-------|
| status_code | int | Ma HTTP (200, 404, 500...) |
| ok | bool | true neu status_code trong 200-299 |
| reason | str | Ly do (OK, Not Found...) |
| headers | vault | Headers tra ve |
| text | str | Noi dung response (body) |
| body | str | Giong text |
| elapsed | float | Thoi gian request (giay) |
| encoding | str | Encoding (utf-8...) |
| url | str | URL cuoi cung (sau redirect) |
| cookies | vault | Cookies tu server |

### Vi du doc response

```mno
hold r = fetch("https://httpbin.org/json")

yell r["status_code"]
yell r["ok"]
yell r["reason"]
yell r["elapsed"]

hold hdrs = r["headers"]
yell hdrs["content-type"]

hold data = response_json(r)
yell data["slideshow"]["title"]
```

Hoac dung cac ham helper:

```mno
yell response_status(r)
yell response_text(r)
yell response_headers(r)
yell response_elapsed(r)
yell response_encoding(r)
yell response_reason(r)
yell response_ok(r)
```

Lay header cu the:

```mno
yell response_header(r, "content-type")
yell response_header(r, "server")
```

## Headers tuy chinh

```mno
hold r = fetch_with_opts("https://httpbin.org/headers", vault {
    headers: vault {
        "X-Custom-Header": "my-value",
        "Authorization": "Bearer my_token"
    }
})
yell r["text"]
```

## Query Parameters

```mno
hold r = fetch_with_opts("https://httpbin.org/get", vault {
    params: vault {
        page: "2",
        limit: "10",
        sort: "name"
    }
})
yell r["text"]
```

## Authentication

### Basic Auth

```mno
hold r = fetch_with_opts("https://httpbin.org/basic-auth/user/pass", vault {
    auth: vault {scheme: "basic", user: "user", pass: "pass"}
})
yell r["status_code"]
yell r["ok"]
```

Hoac dung ham co san:

```mno
hold req = build_request("GET", "https://httpbin.org/basic-auth/user/pass")
set req = set_auth(req, "user", "pass")
```

## Timeout

```mno
hold r = fetch_with_opts("https://httpbin.org/delay/1", vault {
    timeout: 5
})
```

## Proxy

```mno
hold r = fetch_with_opts("https://httpbin.org/ip", vault {
    proxy: "http://127.0.0.1:8080"
})
```

## SSL Verify

```mno
hold r = fetch_with_opts("https://self-signed.example.com", vault {
    verify: false
})
```

## Redirect

```mno
hold r = fetch_with_opts("https://httpbin.org/redirect/1", vault {
    redirect: true
})
yell r["url"]
```

## Retry (thu lai khi loi 5xx)

```mno
hold r = fetch_with_opts("https://httpbin.org/status/503", vault {
    retries: 3
})
yell r["status_code"]
yell r["reason"]
```

## Content-Type

```mno
hold r = fetch_with_opts("https://httpbin.org/post", vault {
    method: "POST",
    data: "data=value",
    content_type: "application/x-www-form-urlencoded"
})
```

## Download File

```mno
hold ok = download_file("https://httpbin.org/image/png", "/tmp/image.png")
check ok
    yell "Download xong!"
or
    yell "That bai!"
end
```

## Grab — Lay noi dung nhanh

```mno
hold text = grab("https://httpbin.org/uuid")
yell text
```

Grab JSON:

```mno
hold data = grab_json("https://httpbin.org/json")
yell data
```

## Status Check

```mno
hold code = status_check("https://httpbin.org/get")
yell code
```

## Ping URL

```mno
hold info = ping_url("https://httpbin.org/get")
yell info["code"]
yell info["total_time"]
yell info["connect_time"]
yell info["reachable"]
```

## Multi Fetch — Request nhieu URL cung luc

```mno
hold urls = []
push "https://httpbin.org/get" into urls
push "https://httpbin.org/ip" into urls
push "https://httpbin.org/uuid" into urls

hold results = multi_fetch(urls)
spin i from 0 to len(results)
    yell results[i]["status_code"]
end
```

## Session — Giu cookie giua cac request

```mno
hold sess = connect()

set sess = session_set_headers(sess, vault {
    "User-Agent": "MyApp/1.0"
})

hold r1 = session_get(sess, "https://httpbin.org/cookies/set/session/abc123")
yell r1["status_code"]

hold r2 = session_get(sess, "https://httpbin.org/cookies")
yell r2["text"]

set sess = session_close(sess)
```

### Session voi POST

```mno
hold sess = connect()
hold r = session_post(sess, "https://httpbin.org/post", vault {key: "value"})
yell r["text"]
set sess = session_close(sess)
```

### Session cookies

```mno
hold sess = connect()
set sess = session_set_cookie(sess, "token", "abc123")
hold cookies = session_get_cookies(sess)
yell cookies

set sess = session_delete_cookie(sess, "token")
set sess = session_clear_cookies(sess)
set sess = session_close(sess)
```

## Tat ca cac ham

### Ham request
- `fetch(url)` — GET don gian
- `fetch_with_opts(url, opts)` — Request voi tuy chinh
- `grab(url)` — GET, tra ve text
- `grab_json(url)` — GET, tra ve dict (parse JSON)
- `grab_headers(url)` — GET, tra ve headers
- `transmit(url, data)` — POST
- `transmit_json(url, data)` — POST JSON
- `transmit_form(url, fields)` — POST form
- `put_data(url, data)` — PUT
- `patch_data(url, data)` — PATCH
- `remove_url(url)` — DELETE
- `head_only(url)` — HEAD
- `options_probe(url)` — OPTIONS
- `download_file(url, save_path)` — Download ve file
- `status_check(url)` — Chi lay status code
- `ping_url(url)` — Thong tin latency
- `multi_fetch(urls)` — Request nhieu URL
- `multi_grab(urls)` — Grab nhieu URL
- `stream_fetch(url)` — Streaming response

### Ham response
- `response_json(resp)` — Parse body thanh dict
- `response_status(resp)` — status_code
- `response_ok(resp)` — ok
- `response_headers(resp)` — headers
- `response_text(resp)` — text
- `response_elapsed(resp)` — elapsed
- `response_encoding(resp)` — encoding
- `response_reason(resp)` — reason
- `response_cookies(resp)` — cookies
- `response_header(resp, name)` — Lay 1 header

### Ham request builder
- `build_request(method, url)` — Tao request
- `set_header(req, key, val)` — Set 1 header
- `set_headers(req, dict)` — Set nhieu header
- `set_body(req, data)` — Set body
- `set_timeout(req, seconds)` — Set timeout
- `set_auth(req, user, pass)` — Set basic auth
- `set_proxy(req, proxy_url)` — Set proxy
- `set_redirect(req, allow)` — Set redirect
- `set_verify(req, verify)` — Set SSL verify
- `req_content_type(req, ctype)` — Set Content-Type
- `req_accept(req, ctype)` — Set Accept
- `req_user_agent(req, agent)` — Set User-Agent
- `req_referer(req, ref)` — Set Referer
- `encode_params(dict)` — Encode query params
- `build_curl_cmd(req)` — Xem curl command

### Ham session
- `connect()` — Tao session moi
- `session_get(sess, url)` — GET
- `session_post(sess, url, data)` — POST
- `session_put(sess, url, data)` — PUT
- `session_delete(sess, url)` — DELETE
- `session_head(sess, url)` — HEAD
- `session_patch(sess, url, data)` — PATCH
- `session_close(sess)` — Dong session
- `session_set_cookie(sess, name, value)` — Set cookie
- `session_get_cookies(sess)` — Lay tat ca cookies
- `session_delete_cookie(sess, name)` — Xoa cookie
- `session_clear_cookies(sess)` — Xoa tat ca cookies
- `session_set_headers(sess, dict)` — Set headers mac dinh
- `session_set_auth(sess, auth_dict)` — Set auth mac dinh
- `session_set_verify(sess, verify)` — Set SSL verify
- `session_set_redirects(sess, max)` — Set max redirects
- `session_cookie_count(sess)` — Dem so cookies

### Ham response parser
- `parse_response(raw)` — Parse raw response
- `extract_headers(raw)` — Parse headers
- `extract_body(raw)` — Parse body
- `parse_json_body(text)` — Parse JSON
- `parse_content_type(header)` — Parse Content-Type
- `parse_cookies_from_header(header)` — Parse cookies

### Ham adapter
- `http_adapter()` — HTTP adapter
- `https_adapter()` — HTTPS adapter
- `retry_adapter(adapter, max_retries)` — Adapter voi retry
- `adapter_execute(adapter, req)` — Thuc thi request
- `adapter_pool_stats(adapter)` — Thong tin pool

## Vi du hoan chinh — API Client

```mno
inp requests

craft get_user agent id
    hold url = "https://jsonplaceholder.typicode.com/users/" + str(id)
    hold r = fetch_with_opts(url, vault {
        headers: vault {"User-Agent": agent}
    })
    check r["ok"]
        give response_json(r)
    or
        raise "Loi " + str(r["status_code"])
    end
end

craft create_post title body
    hold r = transmit_json("https://jsonplaceholder.typicode.com/posts", vault {
        title: title,
        body: body,
        userId: 1
    })
    give response_json(r)
end

hold user = get_user("ManiosApp", 1)
yell user["name"]
yell user["email"]

hold post = create_post("Hello", "Noi dung bai viet")
yell post["id"]
```

## Vi du — Webscraper don gian

```mno
inp requests

hold html = grab("https://httpbin.org/html")
yell "Page length: " + str(len(html))

hold links = []
hold i = 0
spin while i < len(html)
    hold pos = find(substr(html, i, len(html) - i), "href=")
    check pos < 0
        break
    end
    set i = i + pos + 6
    push pos into links
end
yell "Found " + str(len(links)) + " links"
```

## Vi du — Check trang web song hay chet

```mno
inp requests

hold sites = []
push "https://google.com" into sites
push "https://github.com" into sites
push "https://httpbin.org" into sites

spin i from 0 to len(sites)
    hold info = ping_url(sites[i])
    hold status = "LIVE"
    check not info["reachable"]
        set status = "DOWN"
    end
    yell sites[i] + " -> " + status + " (" + str(info["total_time"]) + "s)"
end
```

## Yeu cau

- Can cai dat `curl` tren he thong
  - Termux: `pkg install curl`
  - Ubuntu/Debian: `sudo apt install curl`
  - CentOS/RHEL: `sudo yum install curl`