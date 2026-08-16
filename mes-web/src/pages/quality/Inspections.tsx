// 质量检验记录 (P4-5.5): 列表 + 创建弹窗(含缺陷明细) + 详情抽屉(含缺陷列表)
// 后端 GET/POST /quality/inspections, GET /quality/inspections/{id}
import { useCallback, useEffect, useState } from "react";
import {
    Table, Button, Space, Tag, Modal, Form, Input, InputNumber, Select,
    Drawer, Descriptions, message
} from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface InspectionRow {
    id: number;
    inspection_no: string;
    standard_id: number;
    work_order_id: number;
    work_order_no: string;
    inspector_name: string;
    inspection_type: number;
    sample_qty: number;
    pass_qty: number;
    defect_qty: number;
    result: number;
    remark: string;
    inspected_at: string;
}

interface DefectDetail {
    id: number;
    defect_code: string;
    defect_name: string;
    defect_category: string;
    quantity: number;
    severity: number;
    disposition: number;
    root_cause: string;
    corrective_action: string;
}

interface InspectionDetail extends InspectionRow {
    product_id: number;
    inspector_id: number;
    defects: DefectDetail[];
}

interface PageData<T> {
    total: number;
    list: T[];
}

const RESULT_TEXT: Record<number, { text: string; color: string }> = {
    0: { text: "待检", color: "default" },
    1: { text: "合格", color: "green" },
    2: { text: "不合格", color: "red" },
    3: { text: "让步", color: "orange" }
};

const INSPECTION_TYPE: Record<number, string> = {
    1: "首件检验", 2: "过程检验", 3: "完工检验", 4: "抽样检验"
};

const SEVERITY_TEXT: Record<number, { text: string; color: string }> = {
    1: { text: "轻微", color: "default" },
    2: { text: "一般", color: "orange" },
    3: { text: "严重", color: "red" }
};

const DISPOSITION_TEXT: Record<number, string> = {
    0: "待处理", 1: "返工", 2: "返修", 3: "报废", 4: "让步"
};

export default function Inspections() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<InspectionRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [resultFilter, setResultFilter] = useState<number>(-1);
    const [loading, setLoading] = useState(false);
    const [createOpen, setCreateOpen] = useState(false);
    const [detail, setDetail] = useState<InspectionDetail | null>(null);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (resultFilter >= 0) params.set("result", String(resultFilter));
            const res = await http.get<PageData<InspectionRow>>(`/quality/inspections?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, resultFilter]);

    useEffect(() => { load(); }, [load]);

    async function openDetail(row: InspectionRow) {
        try {
            const d = await http.get<InspectionDetail>(`/quality/inspections/${row.id}`);
            setDetail(d);
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    function openCreate() {
        form.resetFields();
        form.setFieldsValue({
            inspection_type: 2, sample_qty: 1, pass_qty: 0, defect_qty: 0,
            result: -1, defects: []
        });
        setCreateOpen(true);
    }

    async function submit() {
        const values = await form.validateFields();
        try {
            await http.post("/quality/inspections", values);
            message.success("检验记录创建成功");
            setCreateOpen(false);
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    const columns = [
        { title: "检验单号", dataIndex: "inspection_no", width: 180, render: (t: string, row: InspectionRow) => <a onClick={() => openDetail(row)}>{t}</a> },
        { title: "工单号", dataIndex: "work_order_no", width: 160, render: (v: string) => v || "-" },
        { title: "检验员", dataIndex: "inspector_name", width: 100, render: (v: string) => v || "-" },
        {
            title: "类型", dataIndex: "inspection_type", width: 90,
            render: (v: number) => INSPECTION_TYPE[v] ?? v
        },
        { title: "样本量", dataIndex: "sample_qty", width: 80 },
        { title: "合格", dataIndex: "pass_qty", width: 70 },
        { title: "不良", dataIndex: "defect_qty", width: 70 },
        {
            title: "结果", dataIndex: "result", width: 90,
            render: (v: number) => { const r = RESULT_TEXT[v] ?? { text: String(v), color: "default" }; return <Tag color={r.color}>{r.text}</Tag>; }
        },
        { title: "检验时间", dataIndex: "inspected_at", width: 170 }
    ];

    const defectColumns = [
        { title: "缺陷编码", dataIndex: "defect_code", width: 100 },
        { title: "缺陷名称", dataIndex: "defect_name", width: 140 },
        { title: "类别", dataIndex: "defect_category", width: 100, render: (v: string) => v || "-" },
        { title: "数量", dataIndex: "quantity", width: 60 },
        {
            title: "严重度", dataIndex: "severity", width: 80,
            render: (v: number) => { const s = SEVERITY_TEXT[v] ?? { text: String(v), color: "default" }; return <Tag color={s.color}>{s.text}</Tag>; }
        },
        {
            title: "处置", dataIndex: "disposition", width: 80,
            render: (v: number) => DISPOSITION_TEXT[v] ?? v
        }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                <Select
                    placeholder="检验结果"
                    allowClear
                    style={{ width: 140 }}
                    onChange={(v) => { setResultFilter(v ?? -1); setPage(1); }}
                    options={Object.entries(RESULT_TEXT).map(([k, v]) => ({ value: Number(k), label: v.text }))}
                />
                {hasPerm("qc:inspection:add") && (
                    <Button type="primary" onClick={openCreate}>新建检验记录</Button>
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
                title="新建检验记录"
                open={createOpen}
                onOk={submit}
                onCancel={() => setCreateOpen(false)}
                width={640}
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="inspection_type" label="检验类型" rules={[{ required: true }]}>
                        <Select options={Object.entries(INSPECTION_TYPE).map(([k, v]) => ({ value: Number(k), label: v }))} />
                    </Form.Item>
                    <Form.Item name="work_order_id" label="工单 ID">
                        <InputNumber min={0} style={{ width: "100%" }} placeholder="关联工单 (可选)" />
                    </Form.Item>
                    <Form.Item name="standard_id" label="检验标准 ID">
                        <InputNumber min={0} style={{ width: "100%" }} placeholder="关联检验标准 (可选)" />
                    </Form.Item>
                    <Space style={{ width: "100%" }}>
                        <Form.Item name="sample_qty" label="样本量" rules={[{ required: true }]}>
                            <InputNumber min={0} style={{ width: 150 }} />
                        </Form.Item>
                        <Form.Item name="pass_qty" label="合格数">
                            <InputNumber min={0} style={{ width: 150 }} />
                        </Form.Item>
                        <Form.Item name="defect_qty" label="不良数">
                            <InputNumber min={0} style={{ width: 150 }} />
                        </Form.Item>
                    </Space>
                    <Form.Item name="result" label="结果 (留空自动推断)">
                        <Select
                            allowClear
                            options={[
                                { value: 0, label: "待检" },
                                { value: 1, label: "合格" },
                                { value: 2, label: "不合格" },
                                { value: 3, label: "让步" }
                            ]}
                        />
                    </Form.Item>
                    <Form.Item name="remark" label="备注">
                        <Input.TextArea rows={2} />
                    </Form.Item>
                </Form>
            </Modal>

            <Drawer
                title={detail ? `检验记录 - ${detail.inspection_no}` : ""}
                open={!!detail}
                onClose={() => setDetail(null)}
                width={720}
            >
                {detail && (
                    <>
                        <Descriptions column={2} bordered size="small" style={{ marginBottom: 16 }}>
                            <Descriptions.Item label="检验单号">{detail.inspection_no}</Descriptions.Item>
                            <Descriptions.Item label="工单号">{detail.work_order_no || "-"}</Descriptions.Item>
                            <Descriptions.Item label="检验员">{detail.inspector_name || "-"}</Descriptions.Item>
                            <Descriptions.Item label="检验类型">{INSPECTION_TYPE[detail.inspection_type] ?? detail.inspection_type}</Descriptions.Item>
                            <Descriptions.Item label="样本量">{detail.sample_qty}</Descriptions.Item>
                            <Descriptions.Item label="合格/不良">{detail.pass_qty} / {detail.defect_qty}</Descriptions.Item>
                            <Descriptions.Item label="结果">
                                {(() => { const r = RESULT_TEXT[detail.result] ?? { text: String(detail.result), color: "default" }; return <Tag color={r.color}>{r.text}</Tag>; })()}
                            </Descriptions.Item>
                            <Descriptions.Item label="检验时间">{detail.inspected_at}</Descriptions.Item>
                            {detail.remark && <Descriptions.Item label="备注" span={2}>{detail.remark}</Descriptions.Item>}
                        </Descriptions>
                        <Table
                            rowKey="id"
                            columns={defectColumns}
                            dataSource={detail.defects ?? []}
                            size="small"
                            pagination={false}
                            scroll={{ y: 300 }}
                        />
                    </>
                )}
            </Drawer>
        </div>
    );
}
