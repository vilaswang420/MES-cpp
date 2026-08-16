// IoT 设备管理 (P4-5.5): 列表 + 新建/编辑 + 详情抽屉(传感器 Tab) + 删除
// 后端 18 接口已就绪, 复用 WorkOrders.tsx 范式 (Table+Modal+Drawer+hasPerm)
import { useCallback, useEffect, useState } from "react";
import {
    Table, Button, Space, Tag, Modal, Form, Input, InputNumber, Select,
    Drawer, Descriptions, message, Popconfirm, Tabs, Switch
} from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface DeviceRow {
    id: number;
    device_code: string;
    device_name: string;
    type_name: string;
    line_id: number;
    protocol: string;
    ip_address: string;
    port: number;
    status: number;
    last_heartbeat_at: string | null;
    created_at: string;
}

interface SensorRow {
    id: number;
    device_id: number;
    sensor_code: string;
    sensor_name: string;
    data_type: string;
    unit: string;
    register_addr: string;
    alarm_low: number | null;
    alarm_high: number | null;
    sample_interval: number;
    is_key_metric: boolean;
    status: number;
}

interface PageData<T> {
    total: number;
    list: T[];
}

const PROTOCOLS = ["modbus_tcp", "opcua", "mqtt"];
const DEVICE_STATUS: Record<number, { text: string; color: string }> = {
    0: { text: "离线", color: "default" },
    1: { text: "在线", color: "green" }
};

export default function Devices() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<DeviceRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [keyword, setKeyword] = useState("");
    const [loading, setLoading] = useState(false);
    const [editOpen, setEditOpen] = useState(false);
    const [editing, setEditing] = useState<DeviceRow | null>(null);
    const [detail, setDetail] = useState<DeviceRow | null>(null);
    const [sensors, setSensors] = useState<SensorRow[]>([]);
    const [sensorModalOpen, setSensorModalOpen] = useState(false);
    const [form] = Form.useForm();
    const [sensorForm] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (keyword) params.set("keyword", keyword);
            const res = await http.get<PageData<DeviceRow>>(`/iot/devices?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, keyword]);

    useEffect(() => { load(); }, [load]);

    function openCreate() {
        setEditing(null);
        form.resetFields();
        form.setFieldsValue({ protocol: "modbus_tcp", port: 502, status: 0 });
        setEditOpen(true);
    }

    function openEdit(row: DeviceRow) {
        setEditing(row);
        form.setFieldsValue(row);
        setEditOpen(true);
    }

    async function submit() {
        const values = await form.validateFields();
        try {
            if (editing) {
                await http.put(`/iot/devices/${editing.id}`, values);
                message.success("更新成功");
            } else {
                await http.post("/iot/devices", values);
                message.success("创建成功");
            }
            setEditOpen(false);
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function remove(row: DeviceRow) {
        try {
            await http.del(`/iot/devices/${row.id}`);
            message.success("删除成功");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function openDetail(row: DeviceRow) {
        try {
            const d = await http.get<DeviceRow>(`/iot/devices/${row.id}`);
            setDetail(d);
            const s = await http.get<{ list: SensorRow[] }>(`/iot/devices/${row.id}/sensors`);
            setSensors(s.list ?? []);
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    function openAddSensor() {
        sensorForm.resetFields();
        sensorForm.setFieldsValue({ data_type: "float", scale_factor: 1.0, sample_interval: 1000, is_key_metric: false });
        setSensorModalOpen(true);
    }

    async function submitSensor() {
        if (!detail) return;
        const values = await sensorForm.validateFields();
        try {
            await http.post(`/iot/devices/${detail.id}/sensors`, values);
            message.success("传感器添加成功");
            setSensorModalOpen(false);
            const s = await http.get<{ list: SensorRow[] }>(`/iot/devices/${detail.id}/sensors`);
            setSensors(s.list ?? []);
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    const columns = [
        { title: "设备编码", dataIndex: "device_code", width: 140 },
        { title: "设备名称", dataIndex: "device_name", width: 160 },
        { title: "协议", dataIndex: "protocol", width: 100, render: (v: string) => <Tag>{v}</Tag> },
        { title: "IP", dataIndex: "ip_address", width: 130 },
        { title: "端口", dataIndex: "port", width: 70 },
        {
            title: "状态", dataIndex: "status", width: 80,
            render: (v: number) => { const s = DEVICE_STATUS[v] ?? { text: String(v), color: "default" }; return <Tag color={s.color}>{s.text}</Tag>; }
        },
        { title: "最后心跳", dataIndex: "last_heartbeat_at", width: 170, render: (v: string | null) => v ?? "-" },
        {
            title: "操作", width: 200, render: (_: unknown, row: DeviceRow) => (
                <Space>
                    <Button size="small" onClick={() => openDetail(row)}>详情</Button>
                    {hasPerm("iot:device:put") && <Button size="small" onClick={() => openEdit(row)}>编辑</Button>}
                    {hasPerm("iot:device:del") && (
                        <Popconfirm title={`删除设备 ${row.device_code}?`} onConfirm={() => remove(row)}>
                            <Button size="small" danger>删除</Button>
                        </Popconfirm>
                    )}
                </Space>
            )
        }
    ];

    const sensorColumns = [
        { title: "传感器编码", dataIndex: "sensor_code", width: 130 },
        { title: "名称", dataIndex: "sensor_name", width: 120 },
        { title: "类型", dataIndex: "data_type", width: 80 },
        { title: "单位", dataIndex: "unit", width: 60 },
        { title: "寄存器地址", dataIndex: "register_addr", width: 100 },
        { title: "告警下限", dataIndex: "alarm_low", width: 80, render: (v: number | null) => v ?? "-" },
        { title: "告警上限", dataIndex: "alarm_high", width: 80, render: (v: number | null) => v ?? "-" },
        { title: "采样间隔", dataIndex: "sample_interval", width: 90, render: (v: number) => `${v}ms` },
        { title: "关键指标", dataIndex: "is_key_metric", width: 80, render: (v: boolean) => v ? <Tag color="gold">是</Tag> : "-" }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                <Input.Search
                    placeholder="设备编码/名称"
                    allowClear
                    style={{ width: 220 }}
                    onSearch={(v) => { setKeyword(v); setPage(1); }}
                />
                {hasPerm("iot:device:add") && (
                    <Button type="primary" onClick={openCreate}>新增设备</Button>
                )}
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
                title={editing ? "编辑设备" : "新增设备"}
                open={editOpen}
                onOk={submit}
                onCancel={() => setEditOpen(false)}
                width={520}
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="device_code" label="设备编码" rules={[{ required: true }]}>
                        <Input disabled={!!editing} />
                    </Form.Item>
                    <Form.Item name="device_name" label="设备名称" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="protocol" label="协议" rules={[{ required: true }]}>
                        <Select options={PROTOCOLS.map(p => ({ value: p, label: p }))} />
                    </Form.Item>
                    <Form.Item name="ip_address" label="IP 地址">
                        <Input placeholder="如 192.168.1.100" />
                    </Form.Item>
                    <Form.Item name="port" label="端口">
                        <InputNumber min={1} max={65535} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="status" label="初始状态">
                        <Select options={[{ value: 0, label: "离线" }, { value: 1, label: "在线" }]} />
                    </Form.Item>
                </Form>
            </Modal>

            <Drawer
                title={detail ? `设备详情 - ${detail.device_name}` : ""}
                open={!!detail}
                onClose={() => setDetail(null)}
                width={900}
            >
                {detail && (
                    <Tabs
                        items={[
                            {
                                key: "info",
                                label: "基本信息",
                                children: (
                                    <Descriptions column={2} bordered size="small">
                                        <Descriptions.Item label="设备编码">{detail.device_code}</Descriptions.Item>
                                        <Descriptions.Item label="设备名称">{detail.device_name}</Descriptions.Item>
                                        <Descriptions.Item label="协议">{detail.protocol}</Descriptions.Item>
                                        <Descriptions.Item label="IP">{detail.ip_address || "-"}</Descriptions.Item>
                                        <Descriptions.Item label="端口">{detail.port}</Descriptions.Item>
                                        <Descriptions.Item label="状态">
                                            {(() => { const s = DEVICE_STATUS[detail.status] ?? { text: String(detail.status), color: "default" }; return <Tag color={s.color}>{s.text}</Tag>; })()}
                                        </Descriptions.Item>
                                        <Descriptions.Item label="最后心跳">{detail.last_heartbeat_at ?? "-"}</Descriptions.Item>
                                        <Descriptions.Item label="创建时间">{detail.created_at}</Descriptions.Item>
                                    </Descriptions>
                                )
                            },
                            {
                                key: "sensors",
                                label: `传感器 (${sensors.length})`,
                                children: (
                                    <div>
                                        {hasPerm("iot:device:add") && (
                                            <Button type="primary" size="small" style={{ marginBottom: 12 }} onClick={openAddSensor}>
                                                添加传感器
                                            </Button>
                                        )}
                                        <Table
                                            rowKey="id"
                                            columns={sensorColumns}
                                            dataSource={sensors}
                                            size="small"
                                            pagination={false}
                                            scroll={{ y: 400 }}
                                        />
                                    </div>
                                )
                            }
                        ]}
                    />
                )}
            </Drawer>

            <Modal
                title="添加传感器"
                open={sensorModalOpen}
                onOk={submitSensor}
                onCancel={() => setSensorModalOpen(false)}
                width={560}
            >
                <Form form={sensorForm} layout="vertical">
                    <Form.Item name="sensor_code" label="传感器编码" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="sensor_name" label="传感器名称" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="data_type" label="数据类型" rules={[{ required: true }]}>
                        <Select options={["int", "float", "bool", "string"].map(t => ({ value: t, label: t }))} />
                    </Form.Item>
                    <Form.Item name="unit" label="单位">
                        <Input placeholder="如 C / rpm / bar" />
                    </Form.Item>
                    <Form.Item name="register_addr" label="寄存器地址">
                        <Input placeholder="如 40001 (Modbus 保持寄存器)" />
                    </Form.Item>
                    <Form.Item name="scale_factor" label="缩放系数">
                        <InputNumber step={0.1} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="alarm_low" label="告警下限">
                        <InputNumber style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="alarm_high" label="告警上限">
                        <InputNumber style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="sample_interval" label="采样间隔 (ms)">
                        <InputNumber min={100} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="is_key_metric" label="关键指标" valuePropName="checked">
                        <Switch />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
