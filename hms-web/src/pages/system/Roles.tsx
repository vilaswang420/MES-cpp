// 角色管理 (设计文档 4.4 节): 列表/新增/编辑/授权/数据范围
import { useCallback, useEffect, useState } from "react";
import { Table, Button, Space, Modal, Form, Input, Select, message, Popconfirm, Tag } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface RoleRow {
    id: number;
    role_code: string;
    role_name: string;
    data_scope: number;
    status: number;
    sort_order: number;
    remark: string;
}

interface PageData<T> {
    total: number;
    list: T[];
}

// data_scope 5 档 (设计文档 5.4 节)
const SCOPE_OPTIONS = [
    { value: 1, label: "仅本人数据" },
    { value: 2, label: "本部门数据" },
    { value: 3, label: "本部门及子部门" },
    { value: 4, label: "全部数据" },
    { value: 5, label: "自定义部门" }
];

export default function Roles() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<RoleRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [loading, setLoading] = useState(false);
    const [editOpen, setEditOpen] = useState(false);
    const [editRow, setEditRow] = useState<RoleRow | null>(null);
    const [permOpen, setPermOpen] = useState(false);
    const [permRoleId, setPermRoleId] = useState<number>(0);
    const [allPerms, setAllPerms] = useState<{ id: number; perm_code: string; perm_name: string }[]>([]);
    const [checkedPermIds, setCheckedPermIds] = useState<number[]>([]);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const res = await http.get<PageData<RoleRow>>(`/system/roles?page=${page}&page_size=20`);
            setData(res.list);
            setTotal(res.total);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page]);

    useEffect(() => {
        load();
    }, [load]);

    async function openPerm(row: RoleRow) {
        setPermRoleId(row.id);
        const [perms, role] = await Promise.all([
            http.get<{ id: number; perm_code: string; perm_name: string }[]>("/system/permissions/tree"),
            http.get<RoleRow & { permission_ids: number[] }>(`/system/roles/${row.id}`)
        ]);
        setAllPerms(perms);
        setCheckedPermIds(role.permission_ids ?? []);
        setPermOpen(true);
    }

    const columns = [
        { title: "角色编码", dataIndex: "role_code" },
        { title: "角色名称", dataIndex: "role_name" },
        {
            title: "数据范围",
            dataIndex: "data_scope",
            render: (s: number) => <Tag>{SCOPE_OPTIONS.find((o) => o.value === s)?.label ?? s}</Tag>
        },
        { title: "状态", dataIndex: "status", render: (s: number) => (s === 1 ? <Tag color="green">启用</Tag> : <Tag color="red">禁用</Tag>) },
        { title: "排序", dataIndex: "sort_order" },
        {
            title: "操作",
            render: (_: unknown, row: RoleRow) => (
                <Space>
                    {hasPerm("system:role:update") && (
                        <Button
                            size="small"
                            onClick={() => {
                                setEditRow(row);
                                form.setFieldsValue(row);
                                setEditOpen(true);
                            }}
                        >
                            编辑
                        </Button>
                    )}
                    {hasPerm("system:role:assign") && (
                        <Button size="small" onClick={() => openPerm(row)}>
                            授权
                        </Button>
                    )}
                    {hasPerm("system:role:delete") && (
                        <Popconfirm
                            title="确认删除?"
                            onConfirm={async () => {
                                await http.del(`/system/roles/${row.id}`);
                                load();
                            }}
                        >
                            <Button size="small" danger>
                                删除
                            </Button>
                        </Popconfirm>
                    )}
                </Space>
            )
        }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                {hasPerm("system:role:create") && (
                    <Button
                        type="primary"
                        onClick={() => {
                            setEditRow(null);
                            form.resetFields();
                            setEditOpen(true);
                        }}
                    >
                        新增角色
                    </Button>
                )}
            </Space>
            <Table
                rowKey="id"
                loading={loading}
                columns={columns}
                dataSource={data}
                pagination={{ current: page, pageSize: 20, total, onChange: setPage }}
            />
            <Modal
                title={editRow ? "编辑角色" : "新增角色"}
                open={editOpen}
                onOk={async () => {
                    const values = await form.validateFields();
                    if (editRow) await http.put(`/system/roles/${editRow.id}`, values);
                    else await http.post("/system/roles", values);
                    message.success("保存成功");
                    setEditOpen(false);
                    load();
                }}
                onCancel={() => setEditOpen(false)}
                destroyOnClose
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="role_code" label="角色编码" rules={[{ required: true }]}>
                        <Input disabled={!!editRow} />
                    </Form.Item>
                    <Form.Item name="role_name" label="角色名称" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="data_scope" label="数据范围" rules={[{ required: true }]}>
                        <Select options={SCOPE_OPTIONS} />
                    </Form.Item>
                    <Form.Item name="custom_dept_ids" label="自定义部门 ID (数据范围=5 时)" hidden={Form.useWatch("data_scope", form) !== 5}>
                        <Select mode="tags" tokenSeparators={[","]} />
                    </Form.Item>
                    <Form.Item name="sort_order" label="排序">
                        <Input type="number" />
                    </Form.Item>
                    <Form.Item name="remark" label="备注">
                        <Input.TextArea />
                    </Form.Item>
                </Form>
            </Modal>
            <Modal
                title="分配权限"
                open={permOpen}
                width={620}
                onOk={async () => {
                    await http.put(`/system/roles/${permRoleId}/permissions`, { permission_ids: checkedPermIds });
                    message.success("授权已保存 (权限缓存已失效)");
                    setPermOpen(false);
                }}
                onCancel={() => setPermOpen(false)}
            >
                <Select
                    mode="multiple"
                    style={{ width: "100%" }}
                    value={checkedPermIds}
                    onChange={setCheckedPermIds}
                    optionFilterProp="label"
                    options={allPerms.map((p) => ({ value: p.id, label: `${p.perm_code} (${p.perm_name})` }))}
                />
            </Modal>
        </div>
    );
}
