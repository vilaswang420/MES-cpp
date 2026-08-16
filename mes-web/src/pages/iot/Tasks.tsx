// IoT 采集任务管理 (P4-5.5): 列表 + 新建/编辑 + 启停 + 删除
import { useCallback, useEffect, useState } from "react";
import {
    Table, Button, Space, Tag, Modal, Form, Input, InputNumber, Select,
    message, Popconfirm, Switch
} from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface TaskRow {
    id: number;
    task_code: string;
    task_name: string;
    protocol: string;
    interval_ms: number;
    enabled: boolean;
    device_ids: number[];
    created_at: string;
}

interface PageData<T> { total: number; list: T[]; }

const PROTOCOLS = ["modbus_tcp", "opcua", "mqtt"];

export default function Tasks() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<TaskRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [loading, setLoading] = useState(false);
    const [editOpen, setEditOpen] = useState(false);
    const [editing, setEditing] = useState<TaskRow | null>(null);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            const res = await http.get<PageData<TaskRow>>(`/iot/tasks?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page]);

    useEffect(() => { load(); }, [load]);

    function openCreate() {
        setEditing(null);
        form.resetFields();
        form.setFieldsValue({ protocol: "modbus_tcp", interval_ms: 1000, enabled: true, device_ids: [] });
        setEditOpen(true);
    }

    function openEdit(row: TaskRow) {
        setEditing(row);
        form.setFieldsValue(row);
        setEditOpen(true);
    }

    async function submit() {
        const values = await form.validateFields();
        try {
            if (editing) {
                await http.put(`/iot/tasks/${editing.id}`, values);
                message.success("更新成功");
            } else {
                await http.post("/iot/tasks", values);
                message.success("创建成功");
            }
            setEditOpen(false);
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function toggle(row: TaskRow) {
        try {
            await http.put(`/iot/tasks/${row.id}/toggle`);
            message.success(row.enabled ? "已停止" : "已启动");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function remove(row: TaskRow) {
        try {
            await http.del(`/iot/tasks/${row.id}`);
            message.success("删除成功");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    const columns = [
        { title: "任务编码", dataIndex: "task_code", width: 140 },
        { title: "任务名称", dataIndex: "task_name", width: 160 },
        { title: "协议", dataIndex: "protocol", width: 100, render: (v: string) => <Tag>{v}</Tag> },
        { title: "采集间隔", dataIndex: "interval_ms", width: 100, render: (v: number) => `${v}ms` },
        { title: "设备数", dataIndex: "device_ids", width: 80, render: (v: number[]) => v?.length ?? 0 },
        {
            title: "状态", dataIndex: "enabled", width: 80,
            render: (v: boolean) => <Tag color={v ? "green" : "default"}>{v ? "运行中" : "已停止"}</Tag>
        },
        { title: "创建时间", dataIndex: "created_at", width: 170 },
        {
            title: "操作", width: 220, render: (_: unknown, row: TaskRow) => (
                <Space>
                    {hasPerm("iot:task:update") && (
                        <Popconfirm title={row.enabled ? "停止采集?" : "启动采集?"} onConfirm={() => toggle(row)}>
                            <Button size="small">{row.enabled ? "停止" : "启动"}</Button>
                        </Popconfirm>
                    )}
                    {hasPerm("iot:task:update") && <Button size="small" onClick={() => openEdit(row)}>编辑</Button>}
                    {hasPerm("iot:task:delete") && (
                        <Popconfirm title={`删除任务 ${row.task_code}?`} onConfirm={() => remove(row)}>
                            <Button size="small" danger>删除</Button>
                        </Popconfirm>
                    )}
                </Space>
            )
        }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                {hasPerm("iot:task:add") && <Button type="primary" onClick={openCreate}>新增采集任务</Button>}
            </Space>
            <Table
                rowKey="id"
                columns={columns}
                dataSource={data}
                loading={loading}
                size="small"
                pagination={{
                    current: page, pageSize: 20, total,
                    onChange: (p) => setPage(p), showTotal: (t) => `共 ${t} 条`
                }}
            />

            <Modal
                title={editing ? "编辑采集任务" : "新增采集任务"}
                open={editOpen}
                onOk={submit}
                onCancel={() => setEditOpen(false)}
                width={520}
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="task_code" label="任务编码" rules={[{ required: true }]}>
                        <Input disabled={!!editing} />
                    </Form.Item>
                    <Form.Item name="task_name" label="任务名称" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="protocol" label="协议" rules={[{ required: true }]}>
                        <Select options={PROTOCOLS.map(p => ({ value: p, label: p }))} />
                    </Form.Item>
                    <Form.Item name="interval_ms" label="采集间隔 (ms)" rules={[{ required: true }]}>
                        <InputNumber min={100} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="device_ids" label="设备 ID 列表">
                        <Select mode="tags" placeholder="输入设备 ID 后回车" />
                    </Form.Item>
                    <Form.Item name="enabled" label="启用" valuePropName="checked">
                        <Switch />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
