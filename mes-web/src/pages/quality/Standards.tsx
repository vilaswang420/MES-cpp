// 质量检验标准 (P4-5.5): 列表 + 关键字搜索 + 详情抽屉(检验项目列表)
// 后端 GET /quality/standards
import { useCallback, useEffect, useState } from "react";
import { Table, Input, Drawer, Descriptions, Tag, message } from "antd";
import { http } from "../../utils/request";

interface InspectionItem {
    id: number;
    item_code: string;
    item_name: string;
    data_type: string;
    upper_limit: number | null;
    lower_limit: number | null;
    nominal_value: number | null;
    unit: string;
    is_key_item: boolean;
}

interface StandardRow {
    id: number;
    standard_code: string;
    standard_name: string;
    product_id: number;
    product_name: string;
    inspection_type: number;
    sample_size: number;
    aql_level: string;
    items: InspectionItem[];
}

interface PageData<T> {
    total: number;
    list: T[];
}

const INSPECTION_TYPE: Record<number, string> = {
    1: "首件检验",
    2: "过程检验",
    3: "完工检验",
    4: "抽样检验"
};

export default function Standards() {
    const [data, setData] = useState<StandardRow[]>([]);
    const [total, setTotal] = useState(0);
    const [page, setPage] = useState(1);
    const [keyword, setKeyword] = useState("");
    const [loading, setLoading] = useState(false);
    const [detail, setDetail] = useState<StandardRow | null>(null);

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams({ page: String(page), page_size: "20" });
            if (keyword) params.set("keyword", keyword);
            const res = await http.get<PageData<StandardRow>>(`/quality/standards?${params.toString()}`);
            setData(res.list ?? []);
            setTotal(res.total ?? 0);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [page, keyword]);

    useEffect(() => { load(); }, [load]);

    const columns = [
        { title: "标准编码", dataIndex: "standard_code", width: 140, render: (t: string, row: StandardRow) => <a onClick={() => setDetail(row)}>{t}</a> },
        { title: "标准名称", dataIndex: "standard_name", width: 200 },
        { title: "产品", dataIndex: "product_name", width: 160, render: (v: string) => v || "-" },
        {
            title: "检验类型", dataIndex: "inspection_type", width: 100,
            render: (v: number) => <Tag color="blue">{INSPECTION_TYPE[v] ?? v}</Tag>
        },
        { title: "样本量", dataIndex: "sample_size", width: 80 },
        { title: "AQL 等级", dataIndex: "aql_level", width: 100, render: (v: string) => v || "-" },
        { title: "检验项数", width: 90, render: (_: unknown, row: StandardRow) => row.items?.length ?? 0 }
    ];

    const itemColumns = [
        { title: "项目编码", dataIndex: "item_code", width: 120 },
        { title: "项目名称", dataIndex: "item_name", width: 160 },
        { title: "数据类型", dataIndex: "data_type", width: 90 },
        { title: "下限", dataIndex: "lower_limit", width: 80, render: (v: number | null) => v ?? "-" },
        { title: "标称值", dataIndex: "nominal_value", width: 80, render: (v: number | null) => v ?? "-" },
        { title: "上限", dataIndex: "upper_limit", width: 80, render: (v: number | null) => v ?? "-" },
        { title: "单位", dataIndex: "unit", width: 60 },
        {
            title: "关键项", dataIndex: "is_key_item", width: 80,
            render: (v: boolean) => v ? <Tag color="gold">是</Tag> : "-"
        }
    ];

    return (
        <div>
            <Input.Search
                placeholder="标准编码 / 名称"
                allowClear
                style={{ width: 260, marginBottom: 12 }}
                onSearch={(v) => { setKeyword(v); setPage(1); }}
            />
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
            <Drawer
                title={detail ? `检验标准 - ${detail.standard_name}` : ""}
                open={!!detail}
                onClose={() => setDetail(null)}
                width={800}
            >
                {detail && (
                    <>
                        <Descriptions column={2} bordered size="small" style={{ marginBottom: 16 }}>
                            <Descriptions.Item label="标准编码">{detail.standard_code}</Descriptions.Item>
                            <Descriptions.Item label="标准名称">{detail.standard_name}</Descriptions.Item>
                            <Descriptions.Item label="产品">{detail.product_name || "-"}</Descriptions.Item>
                            <Descriptions.Item label="检验类型">{INSPECTION_TYPE[detail.inspection_type] ?? detail.inspection_type}</Descriptions.Item>
                            <Descriptions.Item label="样本量">{detail.sample_size}</Descriptions.Item>
                            <Descriptions.Item label="AQL 等级">{detail.aql_level || "-"}</Descriptions.Item>
                        </Descriptions>
                        <Table
                            rowKey="id"
                            columns={itemColumns}
                            dataSource={detail.items ?? []}
                            size="small"
                            pagination={false}
                            scroll={{ y: 400 }}
                        />
                    </>
                )}
            </Drawer>
        </div>
    );
}
