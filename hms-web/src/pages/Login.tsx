import { useState } from "react";
import { Form, Input, Button, Card, App, Row, Col, Image } from "antd";
import { UserOutlined, LockOutlined, SafetyCertificateOutlined } from "@ant-design/icons";
import { useNavigate } from "react-router-dom";
import { useAuth } from "../store/auth";
import { http } from "../utils/request";
import { ApiError } from "../utils/request";

// 登录页 (计划任务 12): 账密登录; 验证码开关由 sys_configs 控制,
// P3-4.1: 后端返回 SVG data URI (captcha_image), 不再返回明文; 图片点击刷新 (一次一用)。
export default function Login() {
    const { login } = useAuth();
    const navigate = useNavigate();
    const { message } = App.useApp();
    const [loading, setLoading] = useState(false);
    const [captcha, setCaptcha] = useState<{ captcha_id: string; captcha_image: string } | null>(null);

    async function refreshCaptcha() {
        try {
            const data = await http.get<{ captcha_id: string; captcha_image: string }>("/auth/captcha");
            setCaptcha(data);
        } catch {
            // 验证码接口异常不阻塞登录 (dev 环境缺省验证码)
        }
    }

    async function onFinish(values: { username: string; password: string; captcha_code?: string }) {
        setLoading(true);
        try {
            await login(values.username, values.password, captcha
                ? { captcha_id: captcha.captcha_id, captcha_code: values.captcha_code ?? "" }
                : undefined);
            navigate("/", { replace: true });
        } catch (e) {
            const err = e as ApiError;
            message.error(err.traceId ? `${err.message} (trace: ${err.traceId})` : err.message);
            // 登录失败后验证码已消耗 (一次一用), 自动刷新
            void refreshCaptcha();
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
                    {captcha && (
                        <Form.Item
                            name="captcha_code"
                            rules={[{ required: true, message: "请输入验证码" }]}
                        >
                            <Row gutter={8} align="middle">
                                <Col flex="auto">
                                    <Input
                                        prefix={<SafetyCertificateOutlined />}
                                        placeholder="验证码"
                                        maxLength={4}
                                    />
                                </Col>
                                <Col flex="120px">
                                    <Image
                                        src={captcha.captcha_image}
                                        alt="验证码"
                                        preview={false}
                                        style={{
                                            height: 40,
                                            cursor: "pointer",
                                            borderRadius: 4,
                                            display: "block"
                                        }}
                                        onClick={() => void refreshCaptcha()}
                                        title="点击刷新"
                                    />
                                </Col>
                            </Row>
                        </Form.Item>
                    )}
                    <Button type="primary" htmlType="submit" block loading={loading}>
                        登 录
                    </Button>
                    {!captcha && (
                        <Button
                            type="link"
                            block
                            style={{ marginTop: 4 }}
                            onClick={() => void refreshCaptcha()}
                        >
                            启用验证码
                        </Button>
                    )}
                </Form>
            </Card>
        </div>
    );
}
