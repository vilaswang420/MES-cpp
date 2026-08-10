// 审计日志 (设计文档 4.11 节): 分页查询, 只读。
// sys_audit_logs 按月分区, 查询走 created_at DESC 索引。
import { useCallback, useEffect, useState } from "react";
import { Table, Input, InputNumber, Space, Tag, message } from "antd";
import { http } from "../../utils/request";

interface AuditRow {
    id: number;
    user_id: number;
    username: string;
    module: string;
    operation: string;
    method: string;
    request_url: string;
    response_code: number;
    ip_address: string;
    duration_ms: number;
    created_at: string;
}

interface PageData<T> {
    total: number;
    list: T[];
}

export default function AuditLogs() {
    const [data, setData] = useState<AuditRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [userId, setUserId] = useState<number>(0);
    const [module, setModule] = useState("");
    const [loading, setLoading] = useState(false);

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (userId > 0) params.set("user_id", String(userId));
            if (module) params.set("module", module);
            const res = await http.get<PageData<AuditRow>>(`/system/audit-logs?${params.toString()}`);
            setData(res.list);
            setTotal(res.total);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, userId, module]);

    useEffect(() => {
        load();
    }, [load]);

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                <InputNumber
                    placeholder="用户 ID"
                    min={0}
                    onChange={(v) => {
                        setUserId(v ?? 0);
                        setPage(1);
                    }}
                />
                <Input.Search
                    placeholder="模块 (auth/system/production)"
                    allowClear
                    style={{ width: 260 }}
                    onSearch={(v) => {
                        setModule(v);
                        setPage(1);
                    }}
                />
            </Space>
            <Table
                rowKey="id"
                loading={loading}
                dataSource={data}
                pagination={{ current: page, pageSize: 20, total, onChange: setPage }}
                columns={[
                    { title: "时间", dataIndex: "created_at", width: 200 },
                    { title: "用户", dataIndex: "username" },
                    { title: "模块", dataIndex: "module", width: 100 },
                    { title: "操作", dataIndex: "operation" },
                    { title: "方法", dataIndex: "method", width: 80 },
                    { title: "URL", dataIndex: "request_url", ellipsis: true },
                    {
                        title: "状态码",
                        dataIndex: "response_code",
                        width: 90,
                        render: (c: number) => <Tag color={c === 200 ? "green" : "red"}>{c}</Tag>
                    },
                    { title: "耗时(ms)", dataIndex: "duration_ms", width: 100 },
                    { title: "IP", dataIndex: "ip_address", width: 140 }
                ]}
            />
        </div>
    );
}
