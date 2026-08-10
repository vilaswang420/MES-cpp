import { HashRouter, Routes, Route, Navigate } from "react-router-dom";
import { useAuth } from "./store/auth";
import MainLayout from "./layouts/MainLayout";
import Login from "./pages/Login";
import Users from "./pages/system/Users";
import Roles from "./pages/system/Roles";
import Departments from "./pages/system/Departments";
import Permissions from "./pages/system/Permissions";
import AuditLogs from "./pages/system/AuditLogs";
import WorkOrders from "./pages/production/WorkOrders";
import Plans from "./pages/production/Plans";

export default function App() {
    const { user } = useAuth();
    return (
        <HashRouter>
            <Routes>
                <Route path="/login" element={<Login />} />
                <Route
                    path="/"
                    element={user ? <MainLayout /> : <Navigate to="/login" replace />}
                >
                    <Route index element={<Navigate to="/production/work-orders" replace />} />
                    <Route path="system/users" element={<Users />} />
                    <Route path="system/roles" element={<Roles />} />
                    <Route path="system/departments" element={<Departments />} />
                    <Route path="system/permissions" element={<Permissions />} />
                    <Route path="system/audit-logs" element={<AuditLogs />} />
                    <Route path="production/work-orders" element={<WorkOrders />} />
                    <Route path="production/plans" element={<Plans />} />
                </Route>
            </Routes>
        </HashRouter>
    );
}
