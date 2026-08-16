// 缺陷管理 (P4-5.5): 列表 + 处置弹窗 + 状态/类别筛选
// 后端 GET /quality/defects, PUT /quality/defects/{id}/disposition
import { useCallback, useEffect, useState } from "react";
import {
    Table, Button, Space, Tag, Modal, Form, Input, Select, message
} from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface DefectRow {
    id: number;
    inspection_id: number;
    work_order_id: number;
    defect_code: string;
    defect_name: string;
    defect_category: string;
    quantity: number;
    severity: number;
    disposition: number;
    root_cause: string;
    corrective_action: string;
    created_at: string;
}

interface PageData<T> {
    total: number;
    list: T[];
}

const SEVERITY_TEXT: Record<number, { text: string; color: string }> = {
    1: { text: "轻微", color: "default" },
    2: { text: "一般", color: "orange" },
    3: { text: "严重", color: "red" }
};

const DISPOSITION_TEXT: Record<number, { text: string; color: string }> = {
    0: { text: "待处理", color: "default" },
    1: { text: "返工", color: "blue" },
    2: { text: "返修", color: "cyan" },
    3: { text: "报废", color: "red" },
    4: { text: "让步", color: "orange" }
};

export default function Defects() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<DefectRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [dispositionFilter, setDispositionFilter] = useState<number>(-1);
    const [categoryFilter, setCategoryFilter] = useState("");
    const [loading, setLoading] = useState(false);
    const [handleRow, setHandleRow] = useState<DefectRow | null>(null);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (dispositionFilter >= 0) params.set("disposition", String(dispositionFilter));
            if (categoryFilter) params.set("category", categoryFilter);
            const res = await http.get<PageData<DefectRow>>(`/quality/defects?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, dispositionFilter, categoryFilter]);

    useEffect(() => { load(); }, [load]);

    function openHandle(row: DefectRow) {
        form.resetFields();
        form.setFieldsValue({ disposition: undefined, root_cause: row.root_cause, corrective_action: row.corrective_action });
        setHandleRow(row);
    }

    async function submitHandle() {
        if (!handleRow) return;
        const values = await form.validateFields();
        try {
            await http.put(`/quality/defects/${handleRow.id}/disposition`, values);
            message.success("缺陷处置成功");
            setHandleRow(null);
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    const columns = [
        { title: "缺陷编码", dataIndex: "defect_code", width: 110 },
        { title: "缺陷名称", dataIndex: "defect_name", width: 150 },
        { title: "类别", dataIndex: "defect_category", width: 100, render: (v: string) => v || "-" },
        { title: "数量", dataIndex: "quantity", width: 60 },
        {
            title: "严重度", dataIndex: "severity", width: 80,
            render: (v: number) => { const s = SEVERITY_TEXT[v] ?? { text: String(v), color: "default" }; return <Tag color={s.color}>{s.text}</Tag>; }
        },
        {
            title: "处置状态", dataIndex: "disposition", width: 90,
            render: (v: number) => { const d = DISPOSITION_TEXT[v] ?? { text: String(v), color: "default" }; return <Tag color={d.color}>{d.text}</Tag>; }
        },
        { title: "根因", dataIndex: "root_cause", width: 160, ellipsis: true, render: (v: string) => v || "-" },
        { title: "创建时间", dataIndex: "created_at", width: 170 },
        {
            title: "操作", width: 90, render: (_: unknown, row: DefectRow) =>
                row.disposition === 0 && hasPerm("qc:defect:handle") ? (
                    <Button size="small" type="primary" onClick={() => openHandle(row)}>处置</Button>
                ) : null
        }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                <Select
                    placeholder="处置状态"
                    allowClear
                    style={{ width: 140 }}
                    onChange={(v) => { setDispositionFilter(v ?? -1); setPage(1); }}
                    options={Object.entries(DISPOSITION_TEXT).map(([k, v]) => ({ value: Number(k), label: v.text }))}
                />
                <Input.Search
                    placeholder="缺陷类别"
                    allowClear
                    style={{ width: 160 }}
                    onSearch={(v) => { setCategoryFilter(v); setPage(1); }}
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

            <Modal
                title={`缺陷处置 - ${handleRow?.defect_name ?? ""}`}
                open={!!handleRow}
                onOk={submitHandle}
                onCancel={() => setHandleRow(null)}
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="disposition" label="处置方式" rules={[{ required: true }]}>
                        <Select
                            options={[
                                { value: 1, label: "返工" },
                                { value: 2, label: "返修" },
                                { value: 3, label: "报废" },
                                { value: 4, label: "让步" }
                            ]}
                        />
                    </Form.Item>
                    <Form.Item name="root_cause" label="根因分析">
                        <Input.TextArea rows={2} />
                    </Form.Item>
                    <Form.Item name="corrective_action" label="纠正措施">
                        <Input.TextArea rows={2} />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
