// ERP 同步 (P4-5.5): 同步订单 + 转换工单 + 完工上报
// 后端 POST /integration/erp/sync-orders, POST /integration/erp/{id}/convert, POST /integration/erp/report
import { useState } from "react";
import { Card, Form, Input, InputNumber, Button, message, Alert } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

const { TextArea } = Input;

export default function ErpSync() {
    const { hasPerm } = useAuth();
    const [syncForm] = Form.useForm();
    const [convertForm] = Form.useForm();
    const [reportForm] = Form.useForm();
    const [syncResult, setSyncResult] = useState<string | null>(null);
    const [convertResult, setConvertResult] = useState<string | null>(null);
    const [reportResult, setReportResult] = useState<string | null>(null);
    const [loading, setLoading] = useState<string>("");

    async function doSync() {
        const values = await syncForm.validateFields();
        setLoading("sync");
        try {
            let body: unknown;
            try { body = JSON.parse(values.json_body); } catch { return message.error("JSON 格式无效"); }
            const res = await http.post<{ sync_log_id?: number; orders?: unknown[] }>("/integration/erp/sync-orders", body);
            setSyncResult(JSON.stringify(res, null, 2));
            message.success("同步请求已发送");
        } catch (e) {
            message.error((e as Error).message);
        } finally { setLoading(""); }
    }

    async function doConvert() {
        const values = await convertForm.validateFields();
        setLoading("convert");
        try {
            const res = await http.post<{ work_order_id?: number; work_order_no?: string }>(`/integration/erp/${values.order_id}/convert`);
            setConvertResult(JSON.stringify(res, null, 2));
            message.success("转换成功");
        } catch (e) {
            message.error((e as Error).message);
        } finally { setLoading(""); }
    }

    async function doReport() {
        const values = await reportForm.validateFields();
        setLoading("report");
        try {
            const res = await http.post("/integration/erp/report", { work_order_id: values.work_order_id });
            setReportResult(JSON.stringify(res, null, 2));
            message.success("完工上报已发送");
        } catch (e) {
            message.error((e as Error).message);
        } finally { setLoading(""); }
    }

    return (
        <div style={{ maxWidth: 800 }}>
            <Alert
                message="ERP 集成"
                description="从 ERP 系统同步订单、将 ERP 订单转换为 HMS 工单、完工后向 ERP 回报"
                type="info"
                style={{ marginBottom: 16 }}
            />

            <Card title="同步 ERP 订单" size="small" style={{ marginBottom: 16 }}>
                <Form form={syncForm} layout="vertical">
                    <Form.Item name="json_body" label="ERP 订单 JSON" rules={[{ required: true }]}>
                        <TextArea rows={6} placeholder='{"orders": [{"order_no": "SO-2026-001", ...}]}' />
                    </Form.Item>
                    {hasPerm("integ:erp:sync") && (
                        <Button type="primary" loading={loading === "sync"} onClick={doSync}>同步订单</Button>
                    )}
                </Form>
                {syncResult && (
                    <pre style={{ marginTop: 12, padding: 12, background: "#f5f5f5", borderRadius: 4, fontSize: 12, overflow: "auto" }}>{syncResult}</pre>
                )}
            </Card>

            <Card title="转换 ERP 订单为工单" size="small" style={{ marginBottom: 16 }}>
                <Form form={convertForm} layout="vertical">
                    <Form.Item name="order_id" label="ERP 订单 ID" rules={[{ required: true }]}>
                        <InputNumber min={1} style={{ width: "100%" }} />
                    </Form.Item>
                    {hasPerm("integ:erp:convert") && (
                        <Button type="primary" loading={loading === "convert"} onClick={doConvert}>转换工单</Button>
                    )}
                </Form>
                {convertResult && (
                    <pre style={{ marginTop: 12, padding: 12, background: "#f5f5f5", borderRadius: 4, fontSize: 12, overflow: "auto" }}>{convertResult}</pre>
                )}
            </Card>

            <Card title="完工上报 ERP" size="small">
                <Form form={reportForm} layout="vertical">
                    <Form.Item name="work_order_id" label="工单 ID" rules={[{ required: true }]}>
                        <InputNumber min={1} style={{ width: "100%" }} />
                    </Form.Item>
                    {hasPerm("integ:erp:report") && (
                        <Button type="primary" loading={loading === "report"} onClick={doReport}>上报完工</Button>
                    )}
                </Form>
                {reportResult && (
                    <pre style={{ marginTop: 12, padding: 12, background: "#f5f5f5", borderRadius: 4, fontSize: 12, overflow: "auto" }}>{reportResult}</pre>
                )}
            </Card>
        </div>
    );
}
