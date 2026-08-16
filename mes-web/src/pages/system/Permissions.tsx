// 权限管理 (设计文档 4.3 节权限树): 只读树展示 + 权限码检索。
// 权限条目由迁移与 perm_routes.cc 双端维护, 页面不做增删 (防止与 CI 门禁漂移)。
import { useCallback, useEffect, useMemo, useState } from "react";
import { Table, Input, Tag, message } from "antd";
import { http } from "../../utils/request";

interface PermRow {
    id: number;
    perm_code: string;
    perm_name: string;
    perm_type: number; // 1目录 2菜单 3接口
    path: string;
    method: string;
    status: number;
}

const TYPE_TEXT: Record<number, { text: string; color: string }> = {
    1: { text: "目录", color: "blue" },
    2: { text: "菜单", color: "cyan" },
    3: { text: "接口", color: "purple" }
};

export default function Permissions() {
    const [perms, setPerms] = useState<PermRow[]>([]);
    const [keyword, setKeyword] = useState("");

    const load = useCallback(async () => {
        try {
            // 后端返回扁平列表 (含 perm_type), 前端过滤展示
            const tree = await http.get<(PermRow & { children?: PermRow[] })[]>("/system/permissions/tree");
            const flat: PermRow[] = [];
            const walk = (nodes: (PermRow & { children?: PermRow[] })[]) => {
                for (const n of nodes) {
                    flat.push(n);
                    if (n.children) walk(n.children);
                }
            };
            walk(Array.isArray(tree) ? tree : []);
            setPerms(flat);
        } catch (e) {
            message.error((e as Error).message);
        }
    }, []);

    useEffect(() => {
        load();
    }, [load]);

    const filtered = useMemo(
        () =>
            perms.filter(
                (p) =>
                    !keyword ||
                    p.perm_code.includes(keyword) ||
                    p.perm_name.includes(keyword) ||
                    (p.path ?? "").includes(keyword)
            ),
        [perms, keyword]
    );

    return (
        <div>
            <Input.Search
                placeholder="权限码 / 名称 / 路径"
                allowClear
                style={{ width: 300, marginBottom: 12 }}
                onSearch={setKeyword}
            />
            <Table
                rowKey="id"
                dataSource={filtered}
                pagination={{ pageSize: 20 }}
                columns={[
                    { title: "权限码", dataIndex: "perm_code" },
                    { title: "名称", dataIndex: "perm_name" },
                    {
                        title: "类型",
                        dataIndex: "perm_type",
                        render: (t: number) => <Tag color={TYPE_TEXT[t]?.color}>{TYPE_TEXT[t]?.text ?? t}</Tag>
                    },
                    { title: "路径", dataIndex: "path" },
                    { title: "方法", dataIndex: "method", width: 90 },
                    {
                        title: "状态",
                        dataIndex: "status",
                        width: 80,
                        render: (s: number) => (s === 1 ? <Tag color="green">启用</Tag> : <Tag color="red">禁用</Tag>)
                    }
                ]}
            />
        </div>
    );
}
