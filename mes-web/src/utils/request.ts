// 统一请求封装 (计划任务 12):
// - 响应信封 {code,message,data,timestamp,trace_id}, code≠200 一律抛错
// - 401 清会话跳登录
// - 错误提示携带 trace_id 便于排障上报
export interface ApiEnvelope<T> {
    code: number;
    message: string;
    data: T;
    timestamp: string;
    trace_id: string;
}

export class ApiError extends Error {
    code: number;
    traceId: string;
    constructor(code: number, message: string, traceId: string) {
        super(message);
        this.code = code;
        this.traceId = traceId;
    }
}

const BASE = "/api/v1";

function token(): string {
    return localStorage.getItem("mes_access_token") ?? "";
}

export async function request<T>(path: string, options: RequestInit = {}): Promise<T> {
    const headers: Record<string, string> = {
        "Content-Type": "application/json",
        ...(options.headers as Record<string, string>)
    };
    const tk = token();
    if (tk) headers["Authorization"] = `Bearer ${tk}`;

    const resp = await fetch(BASE + path, { ...options, headers });
    let body: ApiEnvelope<T>;
    try {
        body = (await resp.json()) as ApiEnvelope<T>;
    } catch {
        throw new ApiError(resp.status, `响应不是合法 JSON (HTTP ${resp.status})`, "");
    }
    if (body.code !== 200) {
        if (body.code === 401) {
            localStorage.removeItem("mes_access_token");
            localStorage.removeItem("mes_refresh_token");
            localStorage.removeItem("mes_user");
            if (!location.pathname.startsWith("/login")) location.href = "/login";
        }
        throw new ApiError(body.code, body.message, body.trace_id);
    }
    return body.data;
}

export const http = {
    get: <T>(path: string) => request<T>(path),
    post: <T>(path: string, data?: unknown) =>
        request<T>(path, { method: "POST", body: data === undefined ? undefined : JSON.stringify(data) }),
    put: <T>(path: string, data?: unknown) =>
        request<T>(path, { method: "PUT", body: data === undefined ? undefined : JSON.stringify(data) }),
    del: <T>(path: string) => request<T>(path, { method: "DELETE" })
};
