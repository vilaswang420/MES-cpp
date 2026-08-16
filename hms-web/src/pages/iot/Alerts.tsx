// IoT 告警管理 (P4-5.5 + 5.7): 列表 + 确认/消除/忽略
// 告警状态机: 0=未处理 →(ack) 1=已确认 →(resolve) 2=已消除; 0/1 →(dismiss) 3=已忽略
// 告警级别: 1=warning 2=critical
import { useCallback, useEffect, useState } from "react";
import { Table, Button, Space, Tag, Select, message, Popconfirm } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface AlertRow {
    id: number;
    device_id: number;
    device_name: string;
    sensor_id: number;
    alert_type: string;
    alert_level: number;
    alert_value: number | null;
    threshold: number | null;
    message: string;
    status: number;
    acknowledged_by: number | null;
    acknowledged_at: string | null;
    created_at: string;
}

interface PageData<T> { total: number; list: T[]; }

const ALERT_STATUS: Record<number, { text: string; color: string }> = {
    0: { text: "未处理", color: "red" },
    1: { text: "已确认", color: "orange" },
    2: { text: "已消除", color: "green" },
    3: { text: "已忽略", color: "default" }
};

const LEVEL_TEXT: Record<number, { text: string; color: string }> = {
    1: { text: "警告", color: "orange" },
    2: { text: "严重", color: "red" }
};

export default function Alerts() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<AlertRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [status, setStatus] = useState(-1);
    const [level, setLevel] = useState(0);
    const [loading, setLoading] = useState(false);

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (status >= 0) params.set("status", String(status));
            if (level > 0) params.set("level", String(level));
            const res = await http.get<PageData<AlertRow>>(`/iot/alerts?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, status, level]);

    useEffect(() => { load(); }, [load]);

    async function ack(row: AlertRow) {
        try {
            await http.put(`/iot/alerts/${row.id}/acknowledge`);
            message.success("告警已确认");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function resolve(row: AlertRow) {
        try {
            await http.put(`/iot/alerts/${row.id}/resolve`);
            message.success("告警已消除");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function dismiss(row: AlertRow) {
        try {
            await http.put(`/iot/alerts/${row.id}/dismiss`);
            message.success("告警已忽略");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    const columns = [
        { title: "ID", dataIndex: "id", width: 70 },
        { title: "设备", dataIndex: "device_name", width: 140 },
        { title: "类型", dataIndex: "alert_type", width: 100 },
        {
            title: "级别", dataIndex: "alert_level", width: 80,
            render: (v: number) => { const l = LEVEL_TEXT[v] ?? { text: String(v), color: "default" }; return <Tag color={l.color}>{l.text}</Tag>; }
        },
        { title: "告警值", dataIndex: "alert_value", width: 90, render: (v: number | null) => v ?? "-" },
        { title: "阈值", dataIndex: "threshold", width: 90, render: (v: number | null) => v ?? "-" },
        { title: "消息", dataIndex: "message", ellipsis: true },
        {
            title: "状态", dataIndex: "status", width: 90,
            render: (v: number) => { const s = ALERT_STATUS[v] ?? { text: String(v), color: "default" }; return <Tag color={s.color}>{s.text}</Tag>; }
        },
        { title: "确认时间", dataIndex: "acknowledged_at", width: 170, render: (v: string | null) => v ?? "-" },
        { title: "触发时间", dataIndex: "created_at", width: 170 },
        {
            title: "操作", width: 220, render: (_: unknown, row: AlertRow) => (
                <Space>
                    {row.status === 0 && hasPerm("iot:alert:handle") && (
                        <Button size="small" type="primary" onClick={() => ack(row)}>确认</Button>
                    )}
                    {row.status === 1 && hasPerm("iot:alert:resolve") && (
                        <Popconfirm title="标记该告警为已消除?" onConfirm={() => resolve(row)}>
                            <Button size="small" type="primary" ghost>消除</Button>
                        </Popconfirm>
                    )}
                    {(row.status === 0 || row.status === 1) && hasPerm("iot:alert:dismiss") && (
                        <Popconfirm title="忽略该告警 (误报场景)?" onConfirm={() => dismiss(row)}>
                            <Button size="small" danger ghost>忽略</Button>
                        </Popconfirm>
                    )}
                </Space>
            )
        }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                <Select
                    placeholder="状态"
                    allowClear
                    style={{ width: 130 }}
                    onChange={(v) => { setStatus(v ?? -1); setPage(1); }}
                    options={Object.entries(ALERT_STATUS).map(([k, v]) => ({ value: Number(k), label: v.text }))}
                />
                <Select
                    placeholder="级别"
                    allowClear
                    style={{ width: 130 }}
                    onChange={(v) => { setLevel(v ?? 0); setPage(1); }}
                    options={Object.entries(LEVEL_TEXT).map(([k, v]) => ({ value: Number(k), label: v.text }))}
                />
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
        </div>
    );
}
