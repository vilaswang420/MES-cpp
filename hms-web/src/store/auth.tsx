// 会话状态 (JWT + 用户信息 + 权限集), localStorage 持久化。
// 刷新令牌流程: access 过期 -> /auth/refresh 换新对 (request.ts 401 时先兜底跳登录)。
import { createContext, useContext, useState, type ReactNode } from "react";
import { http } from "../utils/request";

export interface SessionUser {
    id: number;
    username: string;
    dept_name?: string;
    roles: string[];
    permissions: string[];
}

interface AuthState {
    user: SessionUser | null;
    login: (
        username: string,
        password: string,
        captcha?: { captcha_id: string; captcha_code: string }
    ) => Promise<void>;
    logout: () => Promise<void>;
    hasPerm: (code: string) => boolean;
}

const AuthContext = createContext<AuthState>(null as unknown as AuthState);

function loadUser(): SessionUser | null {
    const raw = localStorage.getItem("hms_user");
    return raw ? (JSON.parse(raw) as SessionUser) : null;
}

export function AuthProvider({ children }: { children: ReactNode }) {
    const [user, setUser] = useState<SessionUser | null>(loadUser);

    async function login(
        username: string,
        password: string,
        captcha?: { captcha_id: string; captcha_code: string }
    ) {
        // P3-4.1: captcha 可选 (dev 缺省; 生产开关开启后必传)
        const data = await http.post<{
            access_token: string;
            refresh_token: string;
            user: SessionUser;
        }>("/auth/login", { username, password, ...captcha });
        localStorage.setItem("hms_access_token", data.access_token);
        localStorage.setItem("hms_refresh_token", data.refresh_token);
        localStorage.setItem("hms_user", JSON.stringify(data.user));
        setUser(data.user);
    }

    async function logout() {
        try {
            await http.post("/auth/logout");
        } catch {
            // 注销失败不阻塞本地清理
        }
        localStorage.removeItem("hms_access_token");
        localStorage.removeItem("hms_refresh_token");
        localStorage.removeItem("hms_user");
        setUser(null);
    }

    function hasPerm(code: string) {
        if (!user) return false;
        if (user.roles.includes("super_admin")) return true;
        return user.permissions.includes(code);
    }

    return (
        <AuthContext.Provider value={{ user, login, logout, hasPerm }}>
            {children}
        </AuthContext.Provider>
    );
}

export function useAuth() {
    return useContext(AuthContext);
}
