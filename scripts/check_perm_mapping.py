#!/usr/bin/env python3
"""fail-closed 权限映射完整性门禁 (计划任务 8 / 风险缓解"fail-closed 漏注册致新接口 403")。

规则:
  1. 扫描 mes-backend/src/controllers/*.cc 中全部 ADD_METHOD_TO 路由 (path + HTTP method);
  2. 每条路由必须在 src/middlewares/perm_routes.cc 中注册 (addPublic 或 add);
  3. add 注册的权限码 (除 auth:bearer 外) 必须在 migrations/002_seed.up.sql 中
     存在对应 sys_permissions 行 (perm_code), 保证种子库与运行时映射一致。
任一规则不满足 => 退出码 1, CI 构建失败。

用法: python scripts/check_perm_mapping.py [仓库根目录]
"""
import re
import sys
from pathlib import Path

METHOD_MAP = {
    "drogon::Get": "GET",
    "drogon::Post": "POST",
    "drogon::Put": "PUT",
    "drogon::Delete": "DELETE",
    "drogon::Patch": "PATCH",
    "Get": "GET",
    "Post": "POST",
    "Put": "PUT",
    "Delete": "DELETE",
}

# ADD_METHOD_TO(Class::handler, "/path", drogon::Get[, flags...])
ROUTE_RE = re.compile(r'ADD_METHOD_TO\s*\(\s*[\w:]+\s*,\s*"([^"]+)"\s*,\s*([\w:]+)')
# perm_routes.cc: add("/path", "METHOD", "perm:code") / addPublic("/path", "METHOD")
PERM_ADD_RE = re.compile(r'\badd\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"')
PUBLIC_ADD_RE = re.compile(r'\baddPublic\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"')
# 002_seed.up.sql: ('perm_code', ...) 行中的权限码
SEED_CODE_RE = re.compile(r"'([a-z0-9_]+(?::[a-z0-9_]+)+)'")


def normalize(path: str) -> str:
    """统一路由模板写法: {1} / {id} -> {param}"""
    return re.sub(r"\{[^}]*\}", "{param}", path)


def collect_routes(root: Path) -> set:
    routes = set()
    for cc in sorted((root / "mes-backend/src/controllers").glob("*.cc")):
        text = cc.read_text(encoding="utf-8")
        for m in ROUTE_RE.finditer(text):
            method = METHOD_MAP.get(m.group(2))
            if method is None:
                print(f"[WARN] {cc.name}: 未识别的 method 枚举 {m.group(2)}")
                continue
            routes.add((normalize(m.group(1)), method))
    return routes


def collect_perm_map(root: Path):
    text = (root / "mes-backend/src/middlewares/perm_routes.cc").read_text(encoding="utf-8")
    mapping = {}  # (path, method) -> perm code ("__public__" 表示公开)
    for m in PUBLIC_ADD_RE.finditer(text):
        mapping[(normalize(m.group(1)), m.group(2).upper())] = "__public__"
    for m in PERM_ADD_RE.finditer(text):
        mapping[(normalize(m.group(1)), m.group(2).upper())] = m.group(3)
    return mapping


def collect_seed_codes(root: Path) -> set:
    text = (root / "mes-backend/migrations/002_seed.up.sql").read_text(encoding="utf-8")
    return set(SEED_CODE_RE.findall(text))


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    routes = collect_routes(root)
    perm_map = collect_perm_map(root)
    seed_codes = collect_seed_codes(root)

    errors = []

    # 规则 1+2: 每条 Controller 路由必须注册权限映射
    for route in sorted(routes):
        if route not in perm_map:
            errors.append(f"路由未注册权限映射 (fail-closed 将返回 403): {route[1]} {route[0]}")

    # 规则 3: 权限码必须在种子库中存在
    for (path, method), perm in sorted(perm_map.items()):
        if perm in ("__public__", "auth:bearer"):
            continue
        if perm not in seed_codes:
            errors.append(f"权限码未在 002_seed 中播种: {perm} ({method} {path})")

    if errors:
        print("权限映射门禁失败:\n")
        for e in errors:
            print(f"  ✗ {e}")
        print(f"\n共 {len(errors)} 处不一致 (路由 {len(routes)} 条, 映射 {len(perm_map)} 条)")
        return 1

    print(f"权限映射门禁通过: {len(routes)} 条路由全部注册, 权限码与 002_seed 一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
