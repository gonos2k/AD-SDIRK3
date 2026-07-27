import re,math,os,sys
OUTDIR=sys.argv[1] if len(sys.argv)>1 else "dt_ladder_out"
sub=re.compile(r"^\[UTERMS\]\s+sub (\w+)\s+\|T\|=(\S+)")
def series(path,dt):
    step=0; cur=None; rows=[]
    for line in open(path,errors="replace"):
        if "Timing for main" in line: step+=1; continue
        if line.startswith("[UTERMS] rhs="): cur={}; continue
        m=sub.match(line)
        if m and cur is not None:
            try: cur[m.group(1)]=float(m.group(2))
            except ValueError: pass
            if m.group(1)=="horiz" and cur.get("adv_z",0)>0:
                rows.append((step*dt, cur["adv_z"], cur.get("horiz",0.0)))
                cur=None
    return rows
def sigma(rows,t0,t1):
    pts=[(t,v) for t,v,_ in rows if t0<=t<=t1 and v>0]
    agg={}
    for t,v in pts: agg.setdefault(t,[]).append(v)
    xs=sorted(agg); ys=[math.log(sum(agg[t])/len(agg[t])) for t in xs]
    n=len(xs)
    if n<4: return None
    mx=sum(xs)/n; my=sum(ys)/n
    sxx=sum((x-mx)**2 for x in xs); sxy=sum((x-mx)*(y-my) for x,y in zip(xs,ys))
    b=sxy/sxx; a=my-b*mx
    ss=sum((y-(a+b*x))**2 for x,y in zip(xs,ys)); st=sum((y-my)**2 for y in ys)
    r2=1-ss/st if st>0 else float('nan')
    se=math.sqrt(ss/(n-2)/sxx) if n>2 and sxx>0 else float('nan')
    return b,se,r2,n
T0,T1=3600.0,18000.0   # 1h..5h: clear of the initial transient and of the window end
print(f"sigma_eff = d(log|adv_z|)/dt over PHYSICAL time {T0/3600:.0f}h-{T1/3600:.0f}h")
print(f"{'run':<18}{'dt':>5}{'nss':>5}{'dts':>7}{'sigma_eff(1/s)':>17}{'se':>11}{'R2':>7}{'n':>4}")
for tag,dt,nss in [("A_dt600_nss4",600,4),("A_dt300_nss4",300,4),("A_dt150_nss4",150,4),
                   ("B_dt600_nss16",600,16),("B_dt300_nss8",300,8)]:
    p=os.path.join(OUTDIR,f"{tag}.log")
    if not os.path.exists(p): print(f"{tag:<18} missing"); continue
    r=sigma(series(p,dt),T0,T1)
    if r is None: print(f"{tag:<18}{dt:>5}{nss:>5}{dt/nss:>7.1f}   insufficient points"); continue
    b,se,r2,n=r
    print(f"{tag:<18}{dt:>5}{nss:>5}{dt/nss:>7.1f}{b:>17.6e}{se:>11.1e}{r2:>7.3f}{n:>4}")
