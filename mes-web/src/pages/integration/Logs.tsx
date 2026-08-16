// 集成日志 (P4-5.5): 日志列表 + 系统类型/状态筛选 + 重试
// 后端 GET /integration/logs, POST /integration/logs/{id}/retry
import { useCallback, useEffect, useState } from "react";
import { Table, Button, Space, Tag, Select, message, Popconfirm, Modal, Descriptions } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface LogRow {
    id: number;
    system_type: string;
    sync_direction: number;
    sync_type: string;
    business_id: number;
    request_url: string;
    http_status: number;
    duration_ms: number;
    status: number;
    retry_count: number;
    error_msg: string;
    created_at: string;
}

interface PageData<T> {
    total: number;
    list: T[];
}

const LOG_STATUS: Record<number, { text: string; color: string }> = {
    0: { text: "待处理", color: "default" },
    1: { text: "成功", color: "green" },
    2: { text: "失败", color: "red" },
    3: { text: "重试中", color: "orange" }
};

const SYNC_DIRECTION: Record<number, string> = {
    1: "出站 (MES->外部)",
    2: "入站 (外部->MES)"
};

export default function Logs() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<LogRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [systemType, setSystemType] = useState("");
    const [statusFilter, setStatusFilter] = useState<number>(-1);
    const [loading, setLoading] = useState(false);
    const [detail, setDetail] = useState<LogRow | null>(null);

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (systemType) params.set("system_type", systemType);
            if (statusFilter >= 0) params.set("status", String(statusFilter));
            const res = await http.get<PageData<LogRow>>(`/integration/logs?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, systemType, statusFilter]);

    useEffect(() => { load(); }, [load]);

    async function retry(row: LogRow) {
        try {
            await http.post(`/integration/logs/${row.id}/retry`);
            message.success("重试请求已发送");
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    const columns = [
        { title: "ID", dataIndex: "id", width: 70 },
        {
            title: "系统", dataIndex: "system_type", width: 80,
            render: (v: string) => <Tag>{v.toUpperCase()}</Tag>
        },
        { title: "同步类型", dataIndex: "sync_type", width: 120 },
        {
            title: "方向", dataIndex: "sync_direction", width: 130,
            render: (v: number) => SYNC_DIRECTION[v] ?? v
        },
        { title: "业务ID", dataIndex: "business_id", width: 80, render: (v: number) => v || "-" },
        {
            title: "HTTP", dataIndex: "http_status", width: 70,
            render: (v: number) => v > 0 ? <Tag color={v < 400 ? "green" : "red"}>{v}</Tag> : "-"
        },
        { title: "耗时", dataIndex: "duration_ms", width: 70, render: (v: number) => v > 0 ? `${v}ms` : "-" },
        {
            title: "状态", dataIndex: "status", width: 80,
            render: (v: number) => { const s = LOG_STATUS[v] ?? { text: String(v), color: "default" }; return <Tag color={s.color}>{s.text}</Tag>; }
        },
        { title: "重试", dataIndex: "retry_count", width: 50 },
        { title: "时间", dataIndex: "created_at", width: 170 },
        {
            title: "操作", width: 130, render: (_: unknown, row: LogRow) => (
                <Space>
                    <Button size="small" onClick={() => setDetail(row)}>详情</Button>
                    {row.status !== 1 && hasPerm("integ:log:retry") && (
                        <Popconfirm title="确认重试?" onConfirm={() => retry(row)}>
                            <Button size="small" type="primary">重试</Button>
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
                    placeholder="系统类型"
                    allowClear
                    style={{ width: 120 }}
                    onChange={(v) => { setSystemType(v ?? ""); setPage(1); }}
                    options={[
                        { value: "erp", label: "ERP" },
                        { value: "wms", label: "WMS" }
                    ]}
                />
                <Select
                    placeholder="状态"
                    allowClear
                    style={{ width: 120 }}
                    onChange={(v) => { setStatusFilter(v ?? -1); setPage(1); }}
                    options={Object.entries(LOG_STATUS).map(([k, v]) => ({ value: Number(k), label: v.text }))}
                />
            </Space>
            <Table
                rowKey="id"
                columns={columns}
                dataSource={data}
                loading={loading}
                size="small"
                scroll={{ x: 1200 }}
                pagination={{
                    current: page, pageSize: 20, total,
                    onChange: (p) => setPage(p), showTotal: (t) => `共 ${t} 条`
                }}
            />

            <Modal
                title={`日志详情 #${detail?.id ?? ""}`}
                open={!!detail}
                footer={null}
                onCancel={() => setDetail(null)}
                width={640}
            >
                {detail && (
                    <Descriptions column={1} bordered size="small">
                        <Descriptions.Item label="系统类型">{detail.system_type.toUpperCase()}</Descriptions.Item>
                        <Descriptions.Item label="同步方向">{SYNC_DIRECTION[detail.sync_direction] ?? detail.sync_direction}</Descriptions.Item>
                        <Descriptions.Item label="同步类型">{detail.sync_type}</Descriptions.Item>
                        <Descriptions.Item label="业务 ID">{detail.business_id || "-"}</Descriptions.Item>
                        <Descriptions.Item label="请求 URL">{detail.request_url}</Descriptions.Item>
                        <Descriptions.Item label="HTTP 状态">{detail.http_status || "-"}</Descriptions.Item>
                        <Descriptions.Item label="耗时">{detail.duration_ms > 0 ? `${detail.duration_ms}ms` : "-"}</Descriptions.Item>
                        <Descriptions.Item label="状态">{LOG_STATUS[detail.status]?.text ?? detail.status}</Descriptions.Item>
                        <Descriptions.Item label="重试次数">{detail.retry_count}</Descriptions.Item>
                        <Descriptions.Item label="错误信息">{detail.error_msg || "-"}</Descriptions.Item>
                        <Descriptions.Item label="创建时间">{detail.created_at}</Descriptions.Item>
                    </Descriptions>
                )}
            </Modal>
        </div>
    );
}
