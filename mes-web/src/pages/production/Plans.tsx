// 生产计划 (计划任务 16): 列表 + 新建计划 (计划-工单关联由后端维护 prod_plan_work_orders)。
import { useCallback, useEffect, useState } from "react";
import { Table, Button, Modal, Form, InputNumber, DatePicker, Select, message, Tag } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface PlanRow {
    id: number;
    plan_no: string;
    plan_date: string;
    line_id: number;
    line_name: string;
    shift: number;
    plan_qty: number;
    status: number;
    created_at: string;
}

const STATUS_TEXT: Record<number, { text: string; color: string }> = {
    0: { text: "草稿", color: "default" },
    1: { text: "已下达", color: "blue" },
    2: { text: "执行中", color: "processing" },
    3: { text: "已完成", color: "green" },
    4: { text: "已关闭", color: "default" }
};

export default function Plans() {
    const { hasPerm } = useAuth();
    const [data, setData] = useState<PlanRow[]>([]);
    const [lines, setLines] = useState<{ id: number; line_name: string }[]>([]);
    const [loading, setLoading] = useState(false);
    const [createOpen, setCreateOpen] = useState(false);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const res = await http.get<{ list: PlanRow[] } | PlanRow[]>(
                "/production/plans?page=1&page_size=50"
            );
            setData(Array.isArray(res) ? res : res.list);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, []);

    useEffect(() => {
        load();
    }, [load]);

    return (
        <div>
            <Button
                type="primary"
                style={{ marginBottom: 12 }}
                disabled={!hasPerm("prod:plan:add")}
                onClick={async () => {
                    form.resetFields();
                    try {
                        const res = await http.get<{ list: { id: number; line_name: string }[] }>(
                            "/production/lines"
                        );
                        setLines(res.list ?? []);
                    } catch {
                        setLines([]);
                    }
                    setCreateOpen(true);
                }}
            >
                新建计划
            </Button>
            <Table
                rowKey="id"
                loading={loading}
                dataSource={data}
                pagination={{ pageSize: 20 }}
                columns={[
                    { title: "计划编号", dataIndex: "plan_no" },
                    { title: "计划日期", dataIndex: "plan_date", width: 120 },
                    { title: "产线", dataIndex: "line_name" },
                    { title: "班次", dataIndex: "shift", width: 70 },
                    { title: "计划数量", dataIndex: "plan_qty", width: 100 },
                    {
                        title: "状态",
                        dataIndex: "status",
                        width: 100,
                        render: (s: number) => <Tag color={STATUS_TEXT[s]?.color}>{STATUS_TEXT[s]?.text ?? s}</Tag>
                    },
                    { title: "创建时间", dataIndex: "created_at", width: 190 }
                ]}
            />
            <Modal
                title="新建计划"
                open={createOpen}
                onOk={async () => {
                    const values = await form.validateFields();
                    await http.post("/production/plans", {
                        ...values,
                        plan_date: values.plan_date?.format?.("YYYY-MM-DD")
                    });
                    message.success("计划已创建");
                    setCreateOpen(false);
                    load();
                }}
                onCancel={() => setCreateOpen(false)}
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="plan_date" label="计划日期" rules={[{ required: true }]}>
                        <DatePicker style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="line_id" label="产线" rules={[{ required: true }]}>
                        <Select options={lines.map((l) => ({ value: l.id, label: l.line_name }))} />
                    </Form.Item>
                    <Form.Item name="shift" label="班次 (1早/2中/3晚)" rules={[{ required: true }]}>
                        <Select
                            options={[
                                { value: 1, label: "早班" },
                                { value: 2, label: "中班" },
                                { value: 3, label: "晚班" }
                            ]}
                        />
                    </Form.Item>
                    <Form.Item name="plan_qty" label="计划数量" rules={[{ required: true }]}>
                        <InputNumber min={1} style={{ width: "100%" }} />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
