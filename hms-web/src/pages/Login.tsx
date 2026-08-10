import { useState } from "react";
import { Form, Input, Button, Card, App } from "antd";
import { UserOutlined, LockOutlined } from "@ant-design/icons";
import { useNavigate } from "react-router-dom";
import { useAuth } from "../store/auth";
import { ApiError } from "../utils/request";

// 登录页 (计划任务 12): 账密登录; 验证码开关由 sys_configs 控制,
// MVP dev 环境无验证码 (后端 captcha 接口已就绪)。
export default function Login() {
    const { login } = useAuth();
    const navigate = useNavigate();
    const { message } = App.useApp();
    const [loading, setLoading] = useState(false);

    async function onFinish(values: { username: string; password: string }) {
        setLoading(true);
        try {
            await login(values.username, values.password);
            navigate("/", { replace: true });
        } catch (e) {
            const err = e as ApiError;
            message.error(err.traceId ? `${err.message} (trace: ${err.traceId})` : err.message);
        } finally {
            setLoading(false);
        }
    }

    return (
        <div
            style={{
                height: "100vh",
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                background: "linear-gradient(135deg,#1f3b64,#12213a)"
            }}
        >
            <Card title="HMS 制造执行系统" style={{ width: 380 }}>
                <Form onFinish={onFinish} size="large">
                    <Form.Item
                        name="username"
                        rules={[{ required: true, message: "请输入用户名" }]}
                    >
                        <Input prefix={<UserOutlined />} placeholder="用户名 (dev: admin)" />
                    </Form.Item>
                    <Form.Item
                        name="password"
                        rules={[{ required: true, message: "请输入密码" }]}
                    >
                        <Input.Password
                            prefix={<LockOutlined />}
                            placeholder="密码 (dev: password)"
                        />
                    </Form.Item>
                    <Button type="primary" htmlType="submit" block loading={loading}>
                        登 录
                    </Button>
                </Form>
            </Card>
        </div>
    );
}
