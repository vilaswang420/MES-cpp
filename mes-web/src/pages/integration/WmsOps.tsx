// WMS 操作 (P4-5.5): 领料请求 + 入库操作
// 后端 POST /integration/wms/pick-request, POST /integration/wms/stock-in
import { useState } from "react";
import { Card, Form, Input, Button, message, Alert } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

const { TextArea } = Input;

export default function WmsOps() {
    const { hasPerm } = useAuth();
    const [pickForm] = Form.useForm();
    const [stockForm] = Form.useForm();
    const [pickResult, setPickResult] = useState<string | null>(null);
    const [stockResult, setStockResult] = useState<string | null>(null);
    const [loading, setLoading] = useState<string>("");

    async function doPick() {
        const values = await pickForm.validateFields();
        setLoading("pick");
        try {
            let body: unknown;
            try { body = JSON.parse(values.json_body); } catch { return message.error("JSON 格式无效"); }
            const res = await http.post("/integration/wms/pick-request", body);
            setPickResult(JSON.stringify(res, null, 2));
            message.success("领料请求已发送");
        } catch (e) {
            message.error((e as Error).message);
        } finally { setLoading(""); }
    }

    async function doStockIn() {
        const values = await stockForm.validateFields();
        setLoading("stock");
        try {
            let body: unknown;
            try { body = JSON.parse(values.json_body); } catch { return message.error("JSON 格式无效"); }
            const res = await http.post("/integration/wms/stock-in", body);
            setStockResult(JSON.stringify(res, null, 2));
            message.success("入库请求已发送");
        } catch (e) {
            message.error((e as Error).message);
        } finally { setLoading(""); }
    }

    return (
        <div style={{ maxWidth: 800 }}>
            <Alert
                message="WMS 集成"
                description="向 WMS 系统发送领料请求和入库操作"
                type="info"
                style={{ marginBottom: 16 }}
            />

            <Card title="领料请求" size="small" style={{ marginBottom: 16 }}>
                <Form form={pickForm} layout="vertical">
                    <Form.Item name="json_body" label="领料请求 JSON" rules={[{ required: true }]}>
                        <TextArea rows={6} placeholder='{"work_order_id": 1, "materials": [{"material_code": "M001", "qty": 100}]}' />
                    </Form.Item>
                    {hasPerm("integ:wms:pick") && (
                        <Button type="primary" loading={loading === "pick"} onClick={doPick}>发送领料请求</Button>
                    )}
                </Form>
                {pickResult && (
                    <pre style={{ marginTop: 12, padding: 12, background: "#f5f5f5", borderRadius: 4, fontSize: 12, overflow: "auto" }}>{pickResult}</pre>
                )}
            </Card>

            <Card title="入库操作" size="small">
                <Form form={stockForm} layout="vertical">
                    <Form.Item name="json_body" label="入库请求 JSON" rules={[{ required: true }]}>
                        <TextArea rows={6} placeholder='{"product_code": "P001", "qty": 500, "warehouse": "W01"}' />
                    </Form.Item>
                    {hasPerm("integ:wms:inbound") && (
                        <Button type="primary" loading={loading === "stock"} onClick={doStockIn}>发送入库请求</Button>
                    )}
                </Form>
                {stockResult && (
                    <pre style={{ marginTop: 12, padding: 12, background: "#f5f5f5", borderRadius: 4, fontSize: 12, overflow: "auto" }}>{stockResult}</pre>
                )}
            </Card>
        </div>
    );
}
