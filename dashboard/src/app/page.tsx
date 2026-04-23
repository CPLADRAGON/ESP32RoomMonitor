'use client';

import { useEffect, useState, useMemo } from 'react';

// Polyfill for crypto.randomUUID (Required for non-secure IP access)
if (typeof window !== 'undefined' && !window.crypto.randomUUID) {
  // @ts-ignore
  window.crypto.randomUUID = () => {
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => {
      const r = (Math.random() * 16) | 0;
      const v = c === 'x' ? r : (r & 0x3) | 0x8;
      return v.toString(16);
    });
  };
}
import { supabase } from '@/lib/supabase';
import { 
  AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer 
} from 'recharts';

interface Reading {
  id: number;
  created_at: string;
  temperature: number;
  humidity: number;
  ldr_value: number;
  accel_total: number;
  battery_v: number;
  trigger_source: string;
}

interface DeviceLog {
  id: number;
  created_at: string;
  message: string;
  level: string;
}

type Timeframe = 'day' | 'week' | 'month' | 'year';

export default function Dashboard() {
  const [readings, setReadings] = useState<Reading[]>([]);
  const [latest, setLatest] = useState<Reading | null>(null);
  const [logs, setLogs] = useState<DeviceLog[]>([]);
  const [loading, setLoading] = useState(true);
  const [timeframe, setTimeframe] = useState<Timeframe>('day');
  const [realtimeStatus, setRealtimeStatus] = useState<'connecting' | 'online' | 'offline'>('connecting');

  useEffect(() => {
    fetchReadings();
  }, [timeframe]);

  useEffect(() => {
    fetchLogs();
    const channel = supabase
      .channel('live_updates')
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'room_readings' }, (payload) => {
        const newReading = payload.new as Reading;
        setReadings(prev => [...prev.slice(-99), newReading]);
        setLatest(newReading);
      })
      .on('postgres_changes', { event: 'INSERT', schema: 'public', table: 'device_logs' }, (payload) => {
        setLogs(prev => [payload.new as DeviceLog, ...prev].slice(0, 50));
      })
      .subscribe((status) => {
        if (status === 'SUBSCRIBED') setRealtimeStatus('online');
        else if (status === 'CLOSED' || status === 'CHANNEL_ERROR') setRealtimeStatus('offline');
      });

    return () => { supabase.removeChannel(channel); };
  }, []);

  async function fetchLogs() {
    const { data } = await supabase
      .from('device_logs')
      .select('*')
      .order('created_at', { ascending: false })
      .limit(50);
    if (data) setLogs(data);
  }

  const formatSGTime = (dateStr: string) => {
    return new Date(dateStr).toLocaleString('en-SG', { 
      timeZone: 'Asia/Singapore',
      hour12: false,
      hour: '2-digit', minute: '2-digit', second: '2-digit'
    });
  };

  async function fetchReadings() {
    setLoading(true);
    const startDate = new Date();
    if (timeframe === 'day') startDate.setDate(startDate.getDate() - 1);
    else if (timeframe === 'week') startDate.setDate(startDate.getDate() - 7);
    else if (timeframe === 'month') startDate.setMonth(startDate.getMonth() - 1);
    else if (timeframe === 'year') startDate.setFullYear(startDate.getFullYear() - 1);

    const { data, error } = await supabase
      .from('room_readings')
      .select('*')
      .gte('created_at', startDate.toISOString())
      .order('created_at', { ascending: true })
      .limit(500); 

    if (data) {
      setReadings(data);
      if (data.length > 0) setLatest(data[data.length - 1]);
    }
    setLoading(false);
  }

  // Calculate Averages
  const averages = useMemo(() => {
    if (readings.length === 0) return { temp: 0, hum: 0, ldr: 0 };
    const sum = readings.reduce((acc, r) => ({
      temp: acc.temp + r.temperature,
      hum: acc.hum + r.humidity,
      ldr: acc.ldr + r.ldr_value
    }), { temp: 0, hum: 0, ldr: 0 });
    
    return {
      temp: sum.temp / readings.length,
      hum: sum.hum / readings.length,
      ldr: sum.ldr / readings.length
    };
  }, [readings]);

  const displayData = timeframe === 'day' ? {
    temp: latest?.temperature || 0,
    hum: latest?.humidity || 0,
    ldr: latest?.ldr_value || 0,
    label: 'LATEST_SYNC'
  } : {
    temp: averages.temp,
    hum: averages.hum,
    ldr: averages.ldr,
    label: `PERIOD_AVG (${timeframe.toUpperCase()})`
  };

  const chartData = readings.map(r => ({
    time: new Date(r.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false }),
    date: new Date(r.created_at).toLocaleDateString([], { month: 'short', day: 'numeric' }),
    temp: r.temperature,
    hum: r.humidity,
    ldr: (r.ldr_value / 4095) * 100 
  }));

  const lastUpdateStr = latest 
    ? new Date(latest.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false }) 
    : '--:--:--';

  if (loading && readings.length === 0) {
    return (
      <div className="min-h-screen bg-slate-950 flex items-center justify-center">
        <div className="flex flex-col items-center gap-4">
          <div className="w-12 h-12 border-4 border-cyan-400 border-t-transparent rounded-full animate-spin"></div>
          <p className="text-cyan-400 font-bold tracking-widest animate-pulse uppercase">Booting_Aether_OS</p>
        </div>
      </div>
    );
  }

  return (
    <div className="selection:bg-primary-container selection:text-on-primary-container min-h-screen text-on-surface">
      {/* Top Navigation */}
      <header className="fixed top-0 left-0 w-full z-50 flex justify-between items-center px-4 md:px-8 h-16 bg-slate-950/40 backdrop-blur-[20px] border-b border-white/10">
        <div className="flex items-center gap-4">
          <span className="text-lg md:text-xl font-black text-cyan-400 drop-shadow-[0_0_10px_rgba(0,243,255,0.5)]">AETHER_OS</span>
          <div className="hidden sm:block h-4 w-px bg-white/20"></div>
          <nav className="hidden sm:flex gap-8">
            <span className="tracking-widest uppercase text-[10px] md:text-xs font-bold text-cyan-400 border-b-2 border-cyan-400 pb-1 cursor-default">DASHBOARD</span>
          </nav>
        </div>
        <div className="flex items-center gap-3 md:gap-6">
          <div className={`flex items-center gap-2 px-2 md:px-3 py-1 rounded-full border ${
            realtimeStatus === 'online' ? 'bg-green-500/10 border-green-500/50 text-green-400' : 
            realtimeStatus === 'connecting' ? 'bg-amber-500/10 border-amber-500/50 text-amber-400' : 
            'bg-red-500/10 border-red-500/50 text-red-400'
          }`}>
            <span className={`w-1.5 h-1.5 md:w-2 md:h-2 rounded-full ${realtimeStatus === 'online' ? 'bg-green-400 animate-pulse' : 'bg-current'}`}></span>
            <span className="text-[9px] md:text-[10px] font-bold tracking-widest uppercase">{realtimeStatus}</span>
          </div>
          <div className="w-7 h-7 md:w-8 md:h-8 rounded-full overflow-hidden border border-cyan-400/50 bg-slate-800"></div>
        </div>
      </header>

      {/* Desktop Side Navigation */}
      <aside className="hidden md:flex fixed left-0 top-16 bottom-0 z-40 flex-col items-center py-8 bg-slate-950/60 backdrop-blur-[40px] border-r border-white/10 w-20 hover:w-64 transition-all duration-500 group overflow-hidden">
        <div className="flex flex-col items-center group-hover:items-start group-hover:px-6 w-full gap-2 mb-12">
          <div className="w-10 h-10 flex items-center justify-center rounded-lg bg-cyan-500/10 text-cyan-400 active-glow">
            <span className="material-symbols-outlined">blur_on</span>
          </div>
          <div className="hidden group-hover:block transition-all">
            <p className="text-cyan-400 font-bold tracking-tighter text-sm uppercase">Node_01</p>
            <p className="text-[10px] text-cyan-400/60 tracking-widest uppercase">Active</p>
          </div>
        </div>
        <nav className="flex flex-col gap-4 w-full px-3 group-hover:px-4">
          <NavItem icon="dashboard" label="DASHBOARD" active />
          <NavItem icon="sensors" label="SENSORS" />
          <NavItem icon="bolt" label="POWER" />
        </nav>
      </aside>

      {/* Mobile Bottom Navigation */}
      <nav className="md:hidden fixed bottom-0 left-0 w-full h-16 z-50 bg-slate-950/80 backdrop-blur-[20px] border-t border-white/10 flex justify-around items-center px-4">
        <MobileNavItem icon="dashboard" label="HOME" active />
        <MobileNavItem icon="sensors" label="SENSORS" />
        <MobileNavItem icon="bolt" label="POWER" />
      </nav>

      {/* Main Content */}
      <main className="md:pl-20 pt-16 pb-20 md:pb-8">
        <div className="p-4 md:p-8 flex flex-col gap-6 md:gap-8 max-w-[1600px] mx-auto">
          
          <div className="grid grid-cols-12 gap-6 items-end">
            <div className="col-span-12 lg:col-span-8">
              <div className="flex items-center gap-4 mb-2">
                 <p className="text-[12px] text-cyan-400/70 uppercase tracking-[0.3em]">System Monitoring Unit</p>
                 <div className="px-2 py-0.5 bg-slate-900 border border-white/10 rounded text-[10px] text-slate-400 font-mono">
                   SYNC: {lastUpdateStr}
                 </div>
              </div>
              <h1 className="text-2xl md:text-3xl font-semibold text-on-surface uppercase tracking-tight">Environmental_OVR</h1>
            </div>
            <div className="hidden lg:col-span-4 lg:flex justify-end pr-4">
              <div className="flex items-center gap-3 text-xs text-outline font-medium uppercase tracking-widest">
                <span className="flex h-2 w-2 rounded-full bg-tertiary-container shadow-[0_0_8px_#a4f200]"></span>
                LATENCY: 12ms
                <span className="ml-4 flex h-2 w-2 rounded-full bg-primary-container shadow-[0_0_8px_#00f3ff]"></span>
                STATUS: {latest?.trigger_source === 'manual' ? 'MANUAL_TRIGGER' : 'AUTO_SYNC'}
              </div>
            </div>
          </div>

          <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
            <StatTile 
              icon="thermostat" 
              label="TEMPERATURE" 
              value={displayData.temp.toFixed(1)} 
              unit="°C" 
              color="cyan" 
              progress={(displayData.temp) / 50 * 100} 
              subLabel={displayData.label}
            />
            <StatTile 
              icon="humidity_percentage" 
              label="HUMIDITY" 
              value={displayData.hum.toFixed(1)} 
              unit="%" 
              color="magenta" 
              progress={displayData.hum} 
              subLabel={displayData.label}
            />
            <StatTile 
              icon="light_mode" 
              label="LIGHT INTENSITY" 
              value={displayData.ldr.toFixed(0).toLocaleString()} 
              unit="LUX" 
              color="lime" 
              progress={(displayData.ldr) / 4095 * 100} 
              subLabel={displayData.label}
            />
          </div>

          <div className="grid grid-cols-12 gap-6">
            <div className="col-span-12 xl:col-span-8">
              <section className="glass-panel-heavy p-4 md:p-8 rounded-xl h-full flex flex-col min-h-[400px] md:min-h-[500px]">
                <div className="flex flex-col md:flex-row justify-between items-start md:items-center gap-4 mb-6 md:mb-8">
                  <div>
                    <h2 className="text-2xl font-medium text-primary uppercase tracking-tight">Trend_Analysis</h2>
                    <p className="text-[10px] text-outline tracking-wider uppercase">Multi-Sensor Overlay</p>
                  </div>
                  <div className="flex bg-slate-900/50 p-1 rounded-lg border border-white/5 gap-1">
                    <TimeToggle label="24H" active={timeframe === 'day'} onClick={() => setTimeframe('day')} />
                    <TimeToggle label="7D" active={timeframe === 'week'} onClick={() => setTimeframe('week')} />
                    <TimeToggle label="30D" active={timeframe === 'month'} onClick={() => setTimeframe('month')} />
                    <TimeToggle label="1Y" active={timeframe === 'year'} onClick={() => setTimeframe('year')} />
                  </div>
                </div>
                
                <div className="flex-grow min-h-[350px]">
                  <ResponsiveContainer width="100%" height="100%">
                    <AreaChart data={chartData}>
                      <defs>
                        <linearGradient id="cyanGrad" x1="0" y1="0" x2="0" y2="1"><stop offset="5%" stopColor="#00f3ff" stopOpacity={0.3}/><stop offset="95%" stopColor="#00f3ff" stopOpacity={0}/></linearGradient>
                        <linearGradient id="magGrad" x1="0" y1="0" x2="0" y2="1"><stop offset="5%" stopColor="#ff00ff" stopOpacity={0.2}/><stop offset="95%" stopColor="#ff00ff" stopOpacity={0}/></linearGradient>
                        <linearGradient id="limeGrad" x1="0" y1="0" x2="0" y2="1"><stop offset="5%" stopColor="#a4f200" stopOpacity={0.1}/><stop offset="95%" stopColor="#a4f200" stopOpacity={0}/></linearGradient>
                      </defs>
                      <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.05)" vertical={false} />
                      <XAxis dataKey="time" stroke="#849495" fontSize={10} tickLine={false} axisLine={false} interval={timeframe === 'day' ? 2 : 'preserveStartEnd'} />
                      <YAxis stroke="#849495" fontSize={10} tickLine={false} axisLine={false} />
                      <Tooltip contentStyle={{ background: 'rgba(17, 20, 23, 0.9)', border: '1px solid rgba(0, 243, 255, 0.2)', borderRadius: '8px', backdropFilter: 'blur(10px)' }} labelFormatter={(label, payload) => payload && payload[0] ? `${payload[0].payload.date} ${label}` : label} />
                      <Area type="monotone" dataKey="temp" stroke="#00f3ff" fill="url(#cyanGrad)" strokeWidth={3} isAnimationActive={false} />
                      <Area type="monotone" dataKey="hum" stroke="#ff00ff" fill="url(#magGrad)" strokeWidth={2} isAnimationActive={false} />
                      <Area type="monotone" dataKey="ldr" stroke="#a4f200" fill="url(#limeGrad)" strokeWidth={1} isAnimationActive={false} />
                    </AreaChart>
                  </ResponsiveContainer>
                </div>

                <div className="flex gap-6 mt-6 border-t border-white/5 pt-4 overflow-x-auto">
                    <LegendItem color="bg-cyan-400" label="Temperature (°C)" />
                    <LegendItem color="bg-secondary" label="Humidity (%)" />
                    <LegendItem color="bg-tertiary-container" label="Light (Scaled)" />
                </div>
              </section>
            </div>

            <div className="col-span-12 xl:col-span-4">
              <section className="glass-panel-heavy p-4 md:p-8 rounded-xl h-full flex flex-col min-h-[400px] md:min-h-[500px]">
                <div className="flex items-center justify-between mb-6">
                  <div className="flex items-center gap-3">
                    <span className="material-symbols-outlined text-cyan-400">terminal</span>
                    <h2 className="text-xl font-medium text-primary uppercase tracking-tight">System_Logs</h2>
                  </div>
                  <div className="flex items-center gap-2 text-[10px] text-cyan-400/50 font-mono tracking-tighter">
                    <span className="w-1.5 h-1.5 rounded-full bg-cyan-400 animate-pulse"></span>
                    SG_TIME
                  </div>
                </div>
                
                <div className="bg-black/40 rounded-lg p-4 font-mono text-[10px] flex-grow overflow-y-auto flex flex-col gap-3 border border-white/5 custom-scrollbar max-h-[420px]">
                  {logs.length === 0 && (
                    <div className="flex flex-col items-center justify-center h-full gap-2 opacity-30 italic">
                      <span className="material-symbols-outlined animate-spin">sync</span>
                      <p>Awaiting Uplink...</p>
                    </div>
                  )}
                  {logs.map((log) => (
                    <div key={log.id} className="flex flex-col gap-1 border-l-2 border-cyan-500/10 pl-3 hover:border-cyan-400/30 transition-colors py-0.5">
                      <div className="flex justify-between items-center opacity-40">
                        <span>[{formatSGTime(log.created_at)}]</span>
                        <span className={`font-bold ${log.level === 'ERROR' ? 'text-red-400' : 'text-cyan-400'}`}>
                          {log.level}
                        </span>
                      </div>
                      <span className="text-slate-300 leading-relaxed font-light">{log.message}</span>
                    </div>
                  ))}
                </div>

                <div className="mt-4 pt-4 border-t border-white/5 grid grid-cols-2 gap-4">
                  <div className="p-2 bg-slate-900/50 rounded border border-white/5">
                    <p className="text-[9px] text-slate-500 uppercase tracking-widest mb-1">Last Sync</p>
                    <p className="text-[10px] text-cyan-400 font-mono">{logs[0] ? formatSGTime(logs[0].created_at) : '--:--:--'}</p>
                  </div>
                  <div className="p-2 bg-slate-900/50 rounded border border-white/5">
                    <p className="text-[9px] text-slate-500 uppercase tracking-widest mb-1">Log Count</p>
                    <p className="text-[10px] text-cyan-400 font-mono">{logs.length} entries</p>
                  </div>
                </div>
              </section>
            </div>
          </div>
        </div>
      </main>
    </div>
  );
}

function NavItem({ icon, label, active = false }: { icon: string, label: string, active?: boolean }) {
  return (
    <div className={`flex items-center gap-4 p-3 rounded-lg transition-all cursor-pointer ${active ? 'bg-cyan-500/10 text-cyan-400 border-r-4 border-cyan-400' : 'text-slate-500 hover:bg-white/5 hover:text-cyan-200'}`}>
      <span className="material-symbols-outlined">{icon}</span>
      <span className="hidden group-hover:block text-[12px] font-medium tracking-widest">{label}</span>
    </div>
  );
}

function MobileNavItem({ icon, label, active = false }: { icon: string, label: string, active?: boolean }) {
  return (
    <div className={`flex flex-col items-center justify-center gap-1 transition-all cursor-pointer ${active ? 'text-cyan-400' : 'text-slate-500 hover:text-cyan-200'}`}>
      <span className="material-symbols-outlined text-[24px]">{icon}</span>
      <span className="text-[9px] font-bold tracking-widest">{label}</span>
    </div>
  );
}

function TimeToggle({ label, active, onClick }: { label: string, active: boolean, onClick: () => void }) {
  return (
    <button onClick={onClick} className={`px-4 py-1.5 text-[10px] rounded font-bold transition-all duration-300 ${active ? 'bg-cyan-400 text-slate-950 shadow-[0_0_15px_rgba(0,243,255,0.4)]' : 'text-outline hover:text-cyan-200 hover:bg-white/5'}`}>{label}</button>
  );
}

function StatTile({ icon, label, value, unit, color, progress, subLabel }: { icon: string, label: string, value: string, unit: string, color: 'cyan' | 'magenta' | 'lime', progress: number, subLabel: string }) {
  const colorMap = { cyan: 'text-cyan-400 neon-glow-cyan', magenta: 'text-secondary neon-glow-magenta', lime: 'text-tertiary-container neon-glow-lime' };
  const barColorMap = { cyan: 'bg-cyan-400', magenta: 'bg-secondary', lime: 'bg-tertiary-container' };
  return (
    <div className={`glass-panel p-6 rounded-xl flex flex-col gap-4 group transition-all duration-500 ${colorMap[color]} hover:border-current`}>
      <div className="flex justify-between items-start"><span className="material-symbols-outlined active-glow">{icon}</span><span className="text-[10px] font-bold border border-current opacity-30 px-2 py-0.5 rounded tracking-tighter">{subLabel}</span></div>
      <div><p className="text-[12px] text-outline mb-1 font-medium tracking-widest">{label}</p><div className="flex items-baseline gap-1"><span className="text-5xl font-bold text-primary">{value}</span><span className="text-xl font-medium">{unit}</span></div></div>
      <div className="mt-2 h-1 w-full bg-slate-800 rounded-full overflow-hidden"><div className={`h-full ${barColorMap[color]} shadow-lg`} style={{ width: `${Math.min(100, Math.max(0, progress))}%` }}></div></div>
    </div>
  );
}

function LegendItem({ color, label }: { color: string, label: string }) {
  return (
    <div className="flex items-center gap-2 whitespace-nowrap"><span className={`w-3 h-1 ${color} rounded`}></span><span className="text-[10px] text-outline uppercase font-bold tracking-wider">{label}</span></div>
  );
}

function StatusItem({ label, value, active = false }: { label: string, value: string, active?: boolean }) {
  return (
    <div className="bg-white/5 p-4 rounded border border-white/5"><p className="text-[10px] text-outline mb-1 font-bold tracking-widest">{label}</p><p className={`text-lg font-medium ${active ? 'text-cyan-400 active-glow' : 'text-primary'}`}>{value}</p></div>
  );
}
