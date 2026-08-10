// 用户管理 (设计文档 4.3 节): 列表/新增/编辑/启停/重置密码/分配角色
import { useCallback, useEffect, useState } from "react";
import { Table, Button, Space, Input, Select, Modal, Form, message, Popconfirm, Tag } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface UserRow {
    id: number;
    username: string;
    real_name: string;
    employee_no: string;
    dept_name: string;
    phone: string;
    status: number;
    created_at: string;
}

interface PageData<T> {
    total: number;
    list: T[];
}

const STATUS_TEXT: Record<number, { text: string; color: string }> = {
    0: { text: "禁用", color: "red" },
    1: { text: "启用", color: "green" },
    2: { text: "锁定", color: "orange" }
};

export default function Users() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<UserRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [keyword, setKeyword] = useState("");
    const [loading, setLoading] = useState(false);
    const [editOpen, setEditOpen] = useState(false);
    const [editRow, setEditRow] = useState<UserRow | null>(null);
    const [roles, setRoles] = useState<{ id: number; role_name: string }[]>([]);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const kw = keyword ? `&keyword=${encodeURIComponent(keyword)}` : "";
            const res = await http.get<PageData<UserRow>>(`/system/users?page=${page}&page_size=20${kw}`);
            setData(res.list);
            setTotal(res.total);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, keyword]);

    useEffect(() => {
        load();
    }, [load]);

    async function openEdit(row: UserRow | null) {
        setEditRow(row);
        if (roles.length === 0) {
            const r = await http.get<PageData<{ id: number; role_name: string }>>("/system/roles?page=1&page_size=100");
            setRoles(r.list);
        }
        form.resetFields();
        if (row) {
            const detail = await http.get<UserRow & { role_ids: number[] }>(`/system/users/${row.id}`);
            form.setFieldsValue(detail);
        }
        setEditOpen(true);
    }

    async function submit() {
        const values = await form.validateFields();
        if (editRow) {
            await http.put(`/system/users/${editRow.id}`, values);
            if (values.role_ids) await http.put(`/system/users/${editRow.id}/roles`, { role_ids: values.role_ids });
        } else {
            await http.post("/system/users", values);
        }
        message.success("保存成功");
        setEditOpen(false);
        load();
    }

    const columns = [
        { title: "用户名", dataIndex: "username" },
        { title: "姓名", dataIndex: "real_name" },
        { title: "工号", dataIndex: "employee_no" },
        { title: "部门", dataIndex: "dept_name" },
        { title: "电话", dataIndex: "phone" },
        {
            title: "状态",
            dataIndex: "status",
            render: (s: number) => <Tag color={STATUS_TEXT[s]?.color}>{STATUS_TEXT[s]?.text ?? s}</Tag>
        },
        { title: "创建时间", dataIndex: "created_at" },
        {
            title: "操作",
            render: (_: unknown, row: UserRow) => (
                <Space>
                    {hasPerm("system:user:update") && (
                        <Button size="small" onClick={() => openEdit(row)}>
                            编辑
                        </Button>
                    )}
                    {hasPerm("system:user:status") && (
                        <Popconfirm
                            title={row.status === 1 ? "确认禁用?" : "确认启用?"}
                            onConfirm={async () => {
                                await http.put(`/system/users/${row.id}/status`, { status: row.status === 1 ? 0 : 1 });
                                load();
                            }}
                        >
                            <Button size="small">{row.status === 1 ? "禁用" : "启用"}</Button>
                        </Popconfirm>
                    )}
                    {hasPerm("system:user:reset") && (
                        <Popconfirm
                            title="重置为默认密码?"
                            onConfirm={async () => {
                                await http.put(`/system/users/${row.id}/reset-password`);
                                message.success("已重置");
                            }}
                        >
                            <Button size="small" danger>
                                重置密码
                            </Button>
                        </Popconfirm>
                    )}
                    {hasPerm("system:user:delete") && (
                        <Popconfirm
                            title="确认删除?"
                            onConfirm={async () => {
                                await http.del(`/system/users/${row.id}`);
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
                <Input.Search
                    placeholder="用户名/姓名/工号"
                    allowClear
                    onSearch={(v) => {
                        setKeyword(v);
                        setPage(1);
                    }}
                    style={{ width: 240 }}
                />
                <Select
                    placeholder="状态"
                    allowClear
                    style={{ width: 120 }}
                    options={[
                        { value: 1, label: "启用" },
                        { value: 0, label: "禁用" },
                        { value: 2, label: "锁定" }
                    ]}
                    onChange={() => load()}
                />
                {hasPerm("system:user:create") && (
                    <Button type="primary" onClick={() => openEdit(null)}>
                        新增用户
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
                title={editRow ? "编辑用户" : "新增用户"}
                open={editOpen}
                onOk={submit}
                onCancel={() => setEditOpen(false)}
                destroyOnClose
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="username" label="用户名" rules={[{ required: true }]}>
                        <Input disabled={!!editRow} />
                    </Form.Item>
                    {!editRow && (
                        <Form.Item name="password" label="初始密码" rules={[{ required: true, min: 8 }]}>
                            <Input.Password />
                        </Form.Item>
                    )}
                    <Form.Item name="real_name" label="姓名" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="employee_no" label="工号">
                        <Input />
                    </Form.Item>
                    <Form.Item name="dept_id" label="部门 ID">
                        <Input type="number" />
                    </Form.Item>
                    <Form.Item name="phone" label="电话">
                        <Input />
                    </Form.Item>
                    <Form.Item name="email" label="邮箱">
                        <Input />
                    </Form.Item>
                    <Form.Item name="role_ids" label="角色">
                        <Select
                            mode="multiple"
                            options={roles.map((r) => ({ value: r.id, label: r.role_name }))}
                        />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
