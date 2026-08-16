<!-- 告警时间线: 按级别着色 (critical=红 / warning=黄), 最多展示 50 条 -->
<script setup lang="ts">
import { computed } from "vue";

interface AlertItem {
    ts: string;
    device_code: string;
    level: string;
    message: string;
}

const props = defineProps<{
    alerts: AlertItem[];
}>();

// 格式化时间: ISO → HH:MM:SS
function fmtTime(ts: string): string {
    if (!ts) return "";
    const d = new Date(ts);
    return d.toLocaleTimeString("zh-CN", { hour12: false });
}

const displayAlerts = computed(() => props.alerts.slice(0, 50));
</script>

<template>
    <div class="alert-timeline">
        <div v-if="displayAlerts.length === 0" class="empty">暂无告警</div>
        <div v-for="(a, i) in displayAlerts" :key="i" :class="['alert-item', 'lv-' + a.level]">
            <div class="alert-dot"></div>
            <div class="alert-content">
                <div class="alert-header">
                    <span class="alert-time">{{ fmtTime(a.ts) }}</span>
                    <span :class="['alert-level', 'lv-' + a.level]">
                        {{ a.level === "critical" ? "严重" : "警告" }}
                    </span>
                    <span class="alert-device">[{{ a.device_code }}]</span>
                </div>
                <div class="alert-message">{{ a.message }}</div>
            </div>
        </div>
    </div>
</template>

<style scoped>
.alert-timeline {
    height: 100%;
    overflow-y: auto;
    padding: 8px 12px;
}
.empty {
    color: #5b6b8f;
    text-align: center;
    padding: 40px 0;
}
.alert-item {
    display: flex;
    gap: 10px;
    padding: 6px 0;
    border-bottom: 1px solid #112233;
}
.alert-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    margin-top: 6px;
    flex-shrink: 0;
}
.lv-critical .alert-dot {
    background: #f56c6c;
    box-shadow: 0 0 6px #f56c6c;
}
.lv-warning .alert-dot {
    background: #e6a23c;
    box-shadow: 0 0 4px #e6a23c;
}
.alert-content {
    flex: 1;
    min-width: 0;
}
.alert-header {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 11px;
}
.alert-time {
    color: #7c8db5;
}
.alert-level {
    padding: 1px 6px;
    border-radius: 3px;
    font-size: 10px;
    font-weight: 600;
}
.lv-critical .alert-level {
    background: rgba(245, 108, 108, 0.2);
    color: #f87171;
}
.lv-warning .alert-level {
    background: rgba(230, 162, 60, 0.2);
    color: #fbbf24;
}
.alert-device {
    color: #5b6b8f;
}
.alert-message {
    font-size: 12px;
    color: #dbe6ff;
    margin-top: 2px;
    line-height: 1.4;
}
</style>
