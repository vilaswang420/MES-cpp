// 部门管理 (设计文档 4.5 节): 树形结构 + 新增/编辑/删除
// 后端契约: GET /system/departments/tree 返回 data:{tree:[...]};
// 节点字段 id/dept_code/dept_name/sort_order/leader_id/phone/status/children;
// 创建必填 dept_code+dept_name, parent_id/leader_id 传 0 表示空 (NULLIF)。
import { useCallback, useEffect, useState } from "react";
import { Tree, Button, Space, Modal, Form, Input, InputNumber, message, Popconfirm, Card } from "antd";
import { http } from "../../utils/request";
import { useAuth } from "../../store/auth";

interface DeptNode {
    id: number;
    dept_code: string;
    dept_name: string;
    sort_order: number;
    leader_id: number;
    phone: string;
    status: number;
    children?: DeptNode[];
}

interface DeptTreeData {
    key: number;
    title: string;
    raw: DeptNode;
    children?: DeptTreeData[];
}

function toTreeData(nodes: DeptNode[] | undefined | null): DeptTreeData[] {
    if (!Array.isArray(nodes)) return [];
    return nodes.map((n) => ({
        key: n.id,
        title: `${n.dept_name}${n.leader_id ? ` (负责人#${n.leader_id})` : ""}`,
        raw: n,
        children: n.children ? toTreeData(n.children) : []
    }));
}

export default function Departments() {
    const { hasPerm } = useAuth();
    const [tree, setTree] = useState<DeptNode[]>([]);
    const [selected, setSelected] = useState<DeptNode | null>(null);
    const [editOpen, setEditOpen] = useState(false);
    const [form] = Form.useForm();

    const load = useCallback(async () => {
        try {
            // 后端返回 {tree: [...]}; 容错防结构变化致白屏
            const resp = await http.get<{ tree?: DeptNode[] }>("/system/departments/tree");
            setTree(Array.isArray(resp) ? resp : resp?.tree ?? []);
        } catch (e) {
            message.error((e as Error).message);
        }
    }, []);

    useEffect(() => {
        load();
    }, [load]);

    return (
        <div style={{ display: "flex", gap: 16 }}>
            <Card title="部门树" style={{ width: 420 }} extra={
                hasPerm("system:dept:add") && (
                    <Button
                        size="small"
                        type="primary"
                        onClick={() => {
                            form.resetFields();
                            form.setFieldsValue({ parent_id: selected?.id });
                            setEditOpen(true);
                        }}
                    >
                        新增
                    </Button>
                )
            }>
                <Tree
                    treeData={toTreeData(tree)}
                    defaultExpandAll
                    onSelect={(_, info) => setSelected((info.node as unknown as DeptTreeData).raw ?? null)}
                />
            </Card>
            <Card title={selected ? `部门详情 - ${selected.dept_name}` : "部门详情"} style={{ flex: 1 }}>
                {selected ? (
                    <Space direction="vertical">
                        <div>名称: {selected.dept_name}</div>
                        <div>编码: {selected.dept_code || "-"}</div>
                        <div>负责人 ID: {selected.leader_id || "-"}</div>
                        <div>电话: {selected.phone || "-"}</div>
                        <div>排序: {selected.sort_order}</div>
                        <Space>
                            {hasPerm("system:dept:update") && (
                                <Button
                                    onClick={() => {
                                        form.resetFields();
                                        form.setFieldsValue(selected);
                                        setEditOpen(true);
                                    }}
                                >
                                    编辑
                                </Button>
                            )}
                            {hasPerm("system:dept:delete") && (
                                <Popconfirm
                                    title="确认删除该部门? (存在子部门或用户时将失败)"
                                    onConfirm={async () => {
                                        await http.del(`/system/departments/${selected.id}`);
                                        setSelected(null);
                                        load();
                                    }}
                                >
                                    <Button danger>删除</Button>
                                </Popconfirm>
                            )}
                        </Space>
                    </Space>
                ) : (
                    <div style={{ color: "#999" }}>请选择左侧部门</div>
                )}
            </Card>
            <Modal
                title="部门信息"
                open={editOpen}
                onOk={async () => {
                    const values = await form.validateFields();
                    if (selected && values.id) await http.put(`/system/departments/${values.id}`, values);
                    else await http.post("/system/departments", values);
                    message.success("保存成功");
                    setEditOpen(false);
                    load();
                }}
                onCancel={() => setEditOpen(false)}
            >
                <Form form={form} layout="vertical">
                    <Form.Item name="id" hidden>
                        <InputNumber />
                    </Form.Item>
                    <Form.Item name="dept_name" label="部门名称" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="dept_code" label="部门编码" rules={[{ required: true }]}>
                        <Input />
                    </Form.Item>
                    <Form.Item name="parent_id" label="上级部门 ID (空为顶级)">
                        <InputNumber style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="leader_id" label="负责人 ID">
                        <InputNumber style={{ width: "100%" }} />
                    </Form.Item>
                    <Form.Item name="phone" label="电话">
                        <Input />
                    </Form.Item>
                    <Form.Item name="sort_order" label="排序">
                        <InputNumber style={{ width: "100%" }} />
                    </Form.Item>
                </Form>
            </Modal>
        </div>
    );
}
