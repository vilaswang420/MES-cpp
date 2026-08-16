// 主布局 (计划任务 12): 侧边菜单按 permissions 动态过滤 (menu:* 权限码)。
// 菜单树与迁移 002_seed 的 sys_permissions 菜单数据一致。
import { useMemo } from "react";
import { Layout, Menu, Dropdown, Avatar, Space, Tag } from "antd";
import {
    UserOutlined,
    TeamOutlined,
    ApartmentOutlined,
    SafetyOutlined,
    AuditOutlined,
    OrderedListOutlined,
    ScheduleOutlined,
    AppstoreOutlined,
    ApiOutlined,
    AlertOutlined,
    ControlOutlined,
    CheckCircleOutlined,
    SearchOutlined,
    WarningOutlined,
    BarChartOutlined,
    SwapOutlined,
    InboxOutlined,
    FileSearchOutlined
} from "@ant-design/icons";
import { Outlet, useLocation, useNavigate } from "react-router-dom";
import { useAuth } from "../store/auth";

const { Header, Sider, Content } = Layout;

interface MenuNode {
    key: string;
    label: string;
    perm: string; // 需要的菜单权限码
    icon?: React.ReactNode;
    children?: MenuNode[];
}

const MENU_TREE: MenuNode[] = [
    {
        key: "production",
        label: "生产管理",
        perm: "menu:production",
        icon: <AppstoreOutlined />,
        children: [
            { key: "/production/work-orders", label: "工单管理", perm: "menu:production:work-orders", icon: <OrderedListOutlined /> },
            { key: "/production/plans", label: "生产计划", perm: "menu:production:plans", icon: <ScheduleOutlined /> }
        ]
    },
    {
        key: "iot",
        label: "IoT 管理",
        perm: "menu:iot",
        icon: <ApiOutlined />,
        children: [
            { key: "/iot/devices", label: "设备管理", perm: "menu:iot:devices", icon: <ControlOutlined /> },
            { key: "/iot/alerts", label: "告警管理", perm: "menu:iot:alerts", icon: <AlertOutlined /> },
            { key: "/iot/tasks", label: "采集任务", perm: "menu:iot:tasks", icon: <ApiOutlined /> }
        ]
    },
    {
        key: "quality",
        label: "质量管理",
        perm: "menu:quality",
        icon: <CheckCircleOutlined />,
        children: [
            { key: "/quality/standards", label: "检验标准", perm: "menu:quality:standards", icon: <CheckCircleOutlined /> },
            { key: "/quality/inspections", label: "检验记录", perm: "menu:quality:inspections", icon: <SearchOutlined /> },
            { key: "/quality/defects", label: "缺陷管理", perm: "menu:quality:defects", icon: <WarningOutlined /> },
            { key: "/quality/statistics", label: "质量统计", perm: "menu:quality:statistics", icon: <BarChartOutlined /> }
        ]
    },
    {
        key: "integration",
        label: "集成管理",
        perm: "menu:integration",
        icon: <SwapOutlined />,
        children: [
            { key: "/integration/erp-sync", label: "ERP 同步", perm: "menu:integration:erp-sync", icon: <SwapOutlined /> },
            { key: "/integration/wms-ops", label: "WMS 操作", perm: "menu:integration:wms-ops", icon: <InboxOutlined /> },
            { key: "/integration/logs", label: "集成日志", perm: "menu:integration:logs", icon: <FileSearchOutlined /> }
        ]
    },
    {
        key: "system",
        label: "系统管理",
        perm: "menu:system",
        icon: <SafetyOutlined />,
        children: [
            { key: "/system/users", label: "用户管理", perm: "menu:system:users", icon: <UserOutlined /> },
            { key: "/system/roles", label: "角色管理", perm: "menu:system:roles", icon: <TeamOutlined /> },
            { key: "/system/departments", label: "部门管理", perm: "menu:system:departments", icon: <ApartmentOutlined /> },
            { key: "/system/permissions", label: "权限管理", perm: "menu:system:permissions", icon: <SafetyOutlined /> },
            { key: "/system/audit-logs", label: "审计日志", perm: "menu:system:audit-logs", icon: <AuditOutlined /> }
        ]
    }
];

export default function MainLayout() {
    const { user, logout, hasPerm } = useAuth();
    const navigate = useNavigate();
    const location = useLocation();

    // 按权限过滤菜单 (super_admin 在 hasPerm 内直通)
    const items = useMemo(() => {
        return MENU_TREE.filter((g) => hasPerm(g.perm))
            .map((g) => ({
                key: g.key,
                label: g.label,
                icon: g.icon,
                children: (g.children ?? [])
                    .filter((c) => hasPerm(c.perm))
                    .map((c) => ({ key: c.key, label: c.label, icon: c.icon }))
            }))
            .filter((g) => g.children.length > 0);
    }, [hasPerm]);

    return (
        <Layout style={{ minHeight: "100vh" }}>
            <Sider width={220} theme="dark">
                <div style={{ color: "#fff", textAlign: "center", lineHeight: "64px", fontSize: 18, fontWeight: 600 }}>
                    MES 制造执行系统
                </div>
                <Menu
                    theme="dark"
                    mode="inline"
                    items={items}
                    selectedKeys={[location.pathname]}
                    defaultOpenKeys={items.map((i) => i.key)}
                    onClick={({ key }) => navigate(key)}
                />
            </Sider>
            <Layout>
                <Header style={{ background: "#fff", padding: "0 24px", display: "flex", justifyContent: "flex-end" }}>
                    <Dropdown
                        menu={{
                            items: [{ key: "logout", label: "退出登录" }],
                            onClick: async ({ key }) => {
                                if (key === "logout") {
                                    await logout();
                                    navigate("/login");
                                }
                            }
                        }}
                    >
                        <Space style={{ cursor: "pointer" }}>
                            <Avatar icon={<UserOutlined />} />
                            <span>{user?.username}</span>
                            {user?.roles.map((r) => (
                                <Tag key={r}>{r}</Tag>
                            ))}
                        </Space>
                    </Dropdown>
                </Header>
                <Content style={{ margin: 16 }}>
                    <Outlet />
                </Content>
            </Layout>
        </Layout>
    );
}
