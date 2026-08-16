// 质量统计 (P4-5.5): 汇总卡片 + 缺陷类别分布表 + 按日趋势表
// 后端 GET /quality/statistics?start_date=&end_date=
import { useCallback, useEffect, useState } from "react";
import { Card, Row, Col, DatePicker, Table, Statistic, Space, message } from "antd";
import { http } from "../../utils/request";
import dayjs from "dayjs";

const { RangePicker } = DatePicker;

interface Summary {
    total: number;
    pass_cnt: number;
    fail_cnt: number;
    concession_cnt: number;
    defect_total: number;
    first_pass_rate: string;
}

interface CategoryRow {
    category: string;
    count: number;
    quantity: number;
}

interface TrendRow {
    day: string;
    total: number;
    pass_cnt: number;
    defect_qty: number;
}

interface StatData {
    summary: Summary;
    defect_categories: CategoryRow[];
    daily_trend: TrendRow[];
}

export default function Statistics() {
    const [data, setData] = useState<StatData | null>(null);
    const [loading, setLoading] = useState(false);
    const [range, setRange] = useState<[dayjs.Dayjs, dayjs.Dayjs]>([
        dayjs().subtract(30, "day"),
        dayjs()
    ]);

    const load = useCallback(async () => {
        setLoading(true);
        try {
            const params = new URLSearchParams();
            if (range[0]) params.set("start_date", range[0].format("YYYY-MM-DD"));
            if (range[1]) params.set("end_date", range[1].format("YYYY-MM-DD"));
            const res = await http.get<StatData>(`/quality/statistics?${params.toString()}`);
            setData(res);
        } catch (e) {
            message.error((e as Error).message);
        } finally {
            setLoading(false);
        }
    }, [range]);

    useEffect(() => { load(); }, [load]);

    const categoryColumns = [
        { title: "缺陷类别", dataIndex: "category", width: 160 },
        { title: "缺陷次数", dataIndex: "count", width: 100 },
        { title: "缺陷数量", dataIndex: "quantity", width: 100 }
    ];

    const trendColumns = [
        { title: "日期", dataIndex: "day", width: 120 },
        { title: "检验总数", dataIndex: "total", width: 100 },
        { title: "合格数", dataIndex: "pass_cnt", width: 90 },
        { title: "不良数", dataIndex: "defect_qty", width: 90 },
        {
            title: "合格率", width: 100,
            render: (_: unknown, row: TrendRow) =>
                row.total > 0 ? `${((row.pass_cnt / row.total) * 100).toFixed(1)}%` : "-"
        }
    ];

    return (
        <div>
            <Space style={{ marginBottom: 16 }}>
                <RangePicker
                    value={range}
                    onChange={(v) => {
                        if (v && v[0] && v[1]) setRange([v[0], v[1]]);
                    }}
                />
            </Space>

            <Row gutter={16} style={{ marginBottom: 16 }}>
                <Col span={4}>
                    <Card size="small">
                        <Statistic title="检验总数" value={data?.summary?.total ?? 0} loading={loading} />
                    </Card>
                </Col>
                <Col span={4}>
                    <Card size="small">
                        <Statistic title="合格数" value={data?.summary?.pass_cnt ?? 0} loading={loading}
                            valueStyle={{ color: "#52c41a" }} />
                    </Card>
                </Col>
                <Col span={4}>
                    <Card size="small">
                        <Statistic title="不合格数" value={data?.summary?.fail_cnt ?? 0} loading={loading}
                            valueStyle={{ color: "#ff4d4f" }} />
                    </Card>
                </Col>
                <Col span={4}>
                    <Card size="small">
                        <Statistic title="让步接收" value={data?.summary?.concession_cnt ?? 0} loading={loading} />
                    </Card>
                </Col>
                <Col span={4}>
                    <Card size="small">
                        <Statistic title="缺陷总数" value={data?.summary?.defect_total ?? 0} loading={loading} />
                    </Card>
                </Col>
                <Col span={4}>
                    <Card size="small">
                        <Statistic
                            title="一次合格率"
                            value={data?.summary?.first_pass_rate ?? "0"}
                            suffix="%"
                            loading={loading}
                            valueStyle={{ color: "#1890ff" }}
                        />
                    </Card>
                </Col>
            </Row>

            <Row gutter={16}>
                <Col span={10}>
                    <Card title="缺陷类别分布" size="small" loading={loading}>
                        <Table
                            rowKey="category"
                            columns={categoryColumns}
                            dataSource={data?.defect_categories ?? []}
                            size="small"
                            pagination={false}
                            scroll={{ y: 400 }}
                        />
                    </Card>
                </Col>
                <Col span={14}>
                    <Card title="按日趋势" size="small" loading={loading}>
                        <Table
                            rowKey="day"
                            columns={trendColumns}
                            dataSource={data?.daily_trend ?? []}
                            size="small"
                            pagination={false}
                            scroll={{ y: 400 }}
                        />
                    </Card>
                </Col>
            </Row>
        </div>
    );
}
