// 工单管理 (计划任务 16 / 设计文档 4.6 节):
// 列表(data_scope 由后端过滤) + 详情(工序 Tab) + 报工弹窗 + 状态流转按钮(8 态状态机)。
import { useCallback, useEffect, useState } from "react";
import {
    Table, Button, Space, Tag, Modal, Form, Input, InputNumber, Select,
    Drawer, Tabs, Descriptions, message, Popconfirm
} from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface WoRow {
    id: number;
    work_order_no: string;
    product_name: string;
    line_name: string;
    plan_qty: number;
    completed_qty: number;
    status: number;
    priority: number;
    created_at: string;
}

interface WoDetail extends WoRow {
    process_name: string;
    remark: string;
    operations: {
        id: number;
        step_seq: number;
        step_name: string;
        plan_qty: number;
        completed_qty: number;
        good_qty: number;
        defect_qty: number;
        status: number;
    }[];
}

interface PageData<T> {
    total: number;
    list: T[];
}

// 与后端 WorkOrderStateMachine 常量一致
const STATUS_TEXT: Record<number, { text: string; color: string }> = {
    0: { text: "待排产", color: "default" },
    1: { text: "已排产", color: "blue" },
    2: { text: "已下达", color: "cyan" },
    3: { text: "进行中", color: "processing" },
    4: { text: "已暂停", color: "orange" },
    5: { text: "已完工", color: "green" },
    6: { text: "已关闭", color: "default" },
    7: { text: "已取消", color: "red" }
};

// 状态 -> 可执行动作 (与状态机转换表一致, 按钮级提示; 后端仍会二次校验)
const ACTIONS: Record<number, { key: string; label: string; perm: string }[]> = {
    1: [{ key: "release", label: "下达", perm: "prod:wo:release" }],
    2: [{ key: "start", label: "开工", perm: "prod:wo:start" }],
    3: [
        { key: "report", label: "报工", perm: "prod:wo:report" },
        { key: "pause", label: "暂停", perm: "prod:wo:pause" },
        { key: "complete", label: "完工", perm: "prod:wo:complete" }
    ],
    4: [{ key: "start", label: "恢复", perm: "prod:wo:start" }]
};

export default function WorkOrders() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<WoRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [status, setStatus] = useState<number>(-1);
    const [loading, setLoading] = useState(false);
    const [createOpen, setCreateOpen] = useState(false);
    const [detail, setDetail] = useState<WoDetail | null>(null);
    const [reportRow, setReportRow] = useState<WoRow | null>(null);
    const [products, setProducts] = useState<{ id: number; product_name: string }[]>([]);
    const [processes, setProcesses] = useState<{ id: number; process_name: string }[]>([]);
    const [createForm] = Form.useForm();
    const [reportForm] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (status >= 0) params.set("status", String(status));
            const res = await http.get<PageData<WoRow>>(`/production/work-orders?${params.toString()}`);
            setData(res.list);
            setTotal(res.total);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, status]);

    useEffect(() => {
        load();
    }, [load]);

    async function openCreate() {
        createForm.resetFields();
        const [p, proc] = await Promise.all([
            http.get<{ list: { id: number; product_name: string }[] }>("/production/products"),
            http.get<{ list: { id: number; process_name: string }[] }>("/production/processes")
        ]);
        setProducts(p.list ?? []);
        setProcesses(proc.list ?? []);
        setCreateOpen(true);
    }

    async function openDetail(row: WoRow) {
        try {
            setDetail(await http.get<WoDetail>(`/production/work-orders/${row.id}`));
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function doTransition(row: WoRow, action: string) {
        try {
            await http.put(`/production/work-orders/${row.id}/${action}`);
            message.success("操作成功");
            load();
            if (detail?.id === row.id) openDetail(row);
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    async function submitReport() {
        const values = await reportForm.validateFields();
        if (!reportRow) return;
        try {
            await http.post(`/production/work-orders/${reportRow.id}/report`, values);
            message.success("报工成功 (完工时将经 outbox 下发停采指令)");
            setReportRow(null);
            load();
        } catch (e) {
            message.error((e as Error).message);
        }
    }

    return (
        <div>
            <Space style={{ marginBottom: 12 }}>
                <Select
                    placeholder="状态"
                    allowClear
                    style={{ width: 140 }}
                    onChange={(v) => {
                        setStatus(v ?? -1);
                        setPage(1);
                    }}
                    options={Object.entries(STATUS_TEXT).map(([k, v]) => ({ value: Number(k), label: v.text }))}
                />
                {hasPerm("prod:wo:add") && (
                    <Button type="primary" onClick={openCreate}>
                        新建工单
                    </Button>
                )}
            </Space>
            <Table
                rowKey="id"
                loading={loading}
                dataSource={data}
                pagination={{ current: page, pageSize: 20, total, onChange: setPage }}
                columns={[
                    { title: "工单号", dataIndex: "work_order_no", render: (t: string, row: WoRow) => <a onClick={() => openDetail(row)}>{t}</a> },
                    { title: "产品", dataIndex: "product_name" },
                    { title: "产线", dataIndex: "line_name" },
                    { title: "计划数", dataIndex: "plan_qty", width: 90 },
                    { title: "已完成", dataIndex: "completed_qty", width: 90 },
                    {
                        title: "状态",
                        dataIndex: "status",
                        width: 100,
                        render: (s: number) => <Tag color={STATUS_TEXT[s]?.color}>{STATUS_TEXT[s]?.text ?? s}</Tag>
                    },
                    { title: "优先级", dataIndex: "priority", width: 80 },
                    { title: "创建时间", dataIndex: "created_at", width: 190 },
                    {
                        title: "操作",
                        render: (_: unknown, row: WoRow) => (
                            <Space>
                                {(ACTIONS[row.status] ?? [])
                                    .filter((a) => hasPerm(a.perm))
                                    .map((a) =>
                                        a.key === "report" ? (
                                            <Button
                                                key={a.key}
                                                size="small"
                                                type="primary"
                                                onClick={() => {
                                                    reportForm.resetFields();
                                                    setReportRow(row);
                                                }}
                                            >
                                                {a.label}
                                            </Button>
                                        ) : (
                                            <Popconfirm key={a.key} title={`确认${a.label}?`} onConfirm={() => doTransition(row, a.key)}>
                                                <Button size="small">{a.label}</Button>
                                            </Popconfirm>
                                        )
                                    )}
                            </Space>
                        )
                    }
                ]}
            />

            {/* 新建工单 */}
            <Modal
                title="新建工单"
                open={createOpen}
                onOk={async () => {
                    const values = await createForm.validateFields();
                    await http.post("/production/work-orders", values);
                    message.success("创建成功 (初始状态: 待排产)");
                    setCreateOpen(false);
                    load();
                }}
                onCancel={() => setCreateOpen(false)}
            >
                <Form form={createForm} layout="vertical">
                    <Form.Item name="product_id" label="产品" rules={[{ required: true }]}>
                        <Select options={products.map((p) => ({ value: p.id, label: p.product_name }))} />
                    </Form.Item>
                    <Form.Item name="process_id" label="工艺路线" rules={[{ required: true }]}>
                        <Select options={processes.map((p) => ({ value: p.id, label: p.process_name }))} />
                    </Form.Item>
                    <Form.Item name="plan_qty" label="计划数量" rules={[{ required: true }]}>
                        <InputNumber min={1} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="priority" label="优先级 (1-9)">
                        <InputNumber min={1} max={9} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="remark" label="备注">
                        <Input.TextArea />
                    </Form.Item>
                </Form>
            </Modal>

            {/* 详情抽屉: 基本信息 + 工序 Tab */}
            <Drawer title={detail ? `工单 ${detail.work_order_no}` : ""} width={640} open={!!detail} onClose={() => setDetail(null)}>
                {detail && (
                    <>
                        <Descriptions column={2} bordered size="small">
                            <Descriptions.Item label="产品">{detail.product_name}</Descriptions.Item>
                            <Descriptions.Item label="状态">
                                <Tag color={STATUS_TEXT[detail.status]?.color}>{STATUS_TEXT[detail.status]?.text}</Tag>
                            </Descriptions.Item>
                            <Descriptions.Item label="工艺路线">{detail.process_name}</Descriptions.Item>
                            <Descriptions.Item label="产线">{detail.line_name || "-"}</Descriptions.Item>
                            <Descriptions.Item label="计划数">{detail.plan_qty}</Descriptions.Item>
                            <Descriptions.Item label="已完成">{detail.completed_qty}</Descriptions.Item>
                        </Descriptions>
                        <Tabs
                            style={{ marginTop: 16 }}
                            items={(detail.operations ?? []).map((s) => ({
                                key: String(s.id),
                                label: `${s.step_seq}. ${s.step_name}`,
                                children: (
                                    <Descriptions column={1} size="small" bordered>
                                        <Descriptions.Item label="计划数">{s.plan_qty}</Descriptions.Item>
                                        <Descriptions.Item label="已完成">{s.completed_qty}</Descriptions.Item>
                                        <Descriptions.Item label="合格数">{s.good_qty}</Descriptions.Item>
                                        <Descriptions.Item label="不良数">{s.defect_qty}</Descriptions.Item>
                                    </Descriptions>
                                )
                            }))}
                        />
                    </>
                )}
            </Drawer>

            {/* 报工弹窗 (7.5 节事务范式: 工序行级更新 -> 工单汇总 -> 完工写 outbox) */}
            <Modal title={`报工 - ${reportRow?.work_order_no ?? ""}`} open={!!reportRow} onOk={submitReport} onCancel={() => setReportRow(null)}>
                <Form form={reportForm} layout="vertical">
                    <Form.Item name="step_seq" label="工序序号" rules={[{ required: true }]}>
                        <InputNumber min={1} style={{ width: "100%" }} placeholder="对应详情中的工序序号" />
                    </Form.Item>
                    <Form.Item name="good_qty" label="本次合格数量" rules={[{ required: true }]}>
                        <InputNumber min={1} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="defect_qty" label="不良数量">
                        <InputNumber min={0} style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="scrap_qty" label="报废数量">
                        <InputNumber min={0} style={{ width: "100%" }} />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
