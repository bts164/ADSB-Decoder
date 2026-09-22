using DSP, FFTW, Plots, LinearAlgebra, Statistics
plotlyjs()

pow(x) = [real(
    x[i,1]*conj(x[i,1])
) for i in 1:size(x,1)]
dpow(x) = [real(
    x[i,1]*conj(x[i,2]) + conj(x[i,1])*x[i,2]
) for i in 1:size(x,1)]
d2pow(x) = [real(
    x[i,1]*conj(x[i,3]) + 2conj(x[i,2]) * x[i,2] + conj(x[i,1])*x[i,3]
) for i in 1:size(x,1)]

fn = "gqrx_20260825_191707_1089700400_3200000_fc.sigmf-data"
ComplexI16 = ComplexF32#{Int16}
N = filesize(fn) ÷ (sizeof(ComplexF32)*65536)
db(x) = 10log10(max(1e-10, real(x*conj(x))))

x = Array{ComplexF32}(undef, 2^16, N)
read!(fn, x)
#x = ComplexF32.(x) ./ 32768
nf = sqrt(mean(abs2.(x[:])))
x ./= nf
plot(abs.(x[1:1024]))
for i in 1:size(x,2)
    println((i, count(db.(x[:,i]) .> -35)))
end

mutable struct PreambleDetector{T<:Real}
    sps_in::Float64
    sps_out::Int
    h::Array{T}
end

find_bursts = function(x)
    nf = median(db.(x[:]))
    exc = db.(x[:]) .> nf + 10
    clusters = findall(conv(exc, ones(300))[151:end-150] .>= 50)
    return [x[2] for x in zip(clusters[1:end-1], clusters[2:end]) if x[2] - x[1] > 2]
end

bursts = find_bursts(x)
let i = 4
    plot(x[bursts[i]:bursts[i]+500].|>db)
end

X = hcat((fftshift(fft(x[1:1:end,i])) for i in 1:size(x,2))...) ./ length(x[1:1:end,1])
Xds = maximum(db.(reshape(X, 64, 1024, :)), dims=1)[1,:,:]
plot(Xds[:,107])
heatmap(
    #1090 .+ sps .* fftshift(fftfreq(size(X,1))),
    #60e3/spse6 .* (1:size(X,2)),
    Xds',
    c = :jet, size=(800,600),
    xlabel = "Frequency (MHz)", ylabel = "Time (s)")


s = x[53400:53840,107]
snr = maximum(db.(s)) - nf + 1.6
plot(db.(s), marker=:x, ticks=:native)
S = fftshift(fft(s))
S = fftshift(fft(sqrt.(s.*conj.(s))))
S[length(S)÷2] = 0
plot(sps .* fftshift(fftfreq(length(S))), db.(S), ticks = :native)

sfd = resample(pd, s, 8)
sfd /= maximum(abs2.(sfd[:,1])) / sqrt(2)
begin
    plot(pow(sfd))
    plot!(dpow(sfd))
    plot!(d2pow(sfd))
    #vline!(406:8*6:size(sfd,1))
    #vline!(406+24:8*6:size(sfd,1))
end
let N = size(sfd,1)÷24*24
    heatmap(reshape(d2pow(sfd)[1:N], 24, :))
end
plot(6 .* fftshift(fftfreq(length(sfd[:,2]))),
    fftshift(fft(sfd[:,2])).|>db, ticks = :native)

sr = copy(s) #DSP.resample(s, sps/6)

r = PolynomialRatio()


p = PolyphaseFilterBank(65, 16)

function adsb_demod2(p::PolyphaseFilterBank, x; sps = 3.2)
    Np = 16
    Nph = 8
    s = sqrt(2)/maximum(abs.(x))
    pulses = [abs(y) > 0.5 ? 1 : 0 for y in resample(p, s*x, Np / sps)[:]]
    rising = findall(x -> x==1, diff(pulses)) .+ 1
    falling = findall(x -> x==-1, diff(pulses))
    N = min(length(rising), length(falling))
    centers = 0.5 * (rising[1:N] + falling[1:N])
    #return histogram(diff(centers), bins = 100)
    b = [pulses[i] == 1 ? 1 : 0 for i in Int.(round.(centers[1])) .+ (0:Nph:Np*120)]
    b = reshape(b[17:16+2*112], 2, :)
    #display(b[1:1:56])
    nbits = count(b[:] .== 1)
    if nbits < 112
        return (nbits, N)
    end
    m = decode(b[1,:])
    if 0 != crc(m)
        m = decode((b[2,:] .+ 1) .% 2)
    end
    #return (N, m, crc(m), unpack(m))
    return begin
        plot(pulses, size = (1600,800))
        #plot!(centers[1:end-1], diff(centers) ./ 32)
        #vline!(rising)
        #vline!(falling)
        vline!(centers[1] .+ (0:Np:Np*122))
        vline!(centers[1] .+ (Nph:Np:Np*122))
    end
end
[(i, adsb_demod2(p, x[j:j+500])) for (i,j) in enumerate(bursts[30:30])][1][2]

function adsb_demod(x, N = 10; sps = 3.2)
    s = x .* sqrt(2)/maximum(abs.(x))
    #sa = Float32[s > 0.5 ? 1 : 0 for s in abs.(s)]
    #sa = conv(sa, ones(20))[11:end-10]
    # t0 = max(1, findfirst(sa .>= 4) - 10)
    # t1 = min(length(s), max(t0 + Int(round(sps * 150)), findlast(sa .>= 4)+10))
    # println((t0, t1))
    # return begin
    #     plot(abs.(s))
    #     plot!(sa)
    #     vline!([t0, t1])
    # end
    # s = s[t0:t1]

    Nt = Int(round(length(s) * 2/sps)) + 2
    τ = zeros(Nt,N+1)
    rate = 0.5*ones(Nt,N+1)
    calc_t(i) = rate[:,i] .* (1:Nt) .+ τ[:,i]

    p = PolyphaseFilterBank(101, 64, 5)
    y = make_interp(p, s, sps)
    dr = .1
    hr = digitalfilter(Lowpass(8/48), FIRWindow(hanning(201)))
    hτ = digitalfilter(Lowpass(8/48), FIRWindow(hanning(201)))
    Mh = length(hr) ÷ 2
    v = zeros(Float32, size(τ,1), 3)
    for i in 1:N
        t = calc_t(i)
        #display(t)
        for i in 1:Nt
            v[i,:] = y(t[i])
        end
        #t .+= [y > 0 ? 0 : 1 for y in v[:,3]] .* dr .* v[:,2]
        #sign.(v[:,3]) .* v[:,2]
        #rate[:,i+1] = conv(clamp.(t ./ (1:Nt), 0.45, 0.55), hr)[Mh+1:end-Mh]
        #τ[:,i+1] = conv(clamp.(t .- rate[:,i+1] .* (1:Nt), -0.25, 0.25), hτ)[Mh+1:end-Mh]
        #τ[:,i+1] = clamp.(t .- [0;cumsum(rate[:,i+1])], -0.25, 0.25)
        #display((rate, τ[:,i+1]))
        #τ[:,i+1] = clamp.(conv(τ[:,i],h)[Mh+1:end-Mh], -0.1, 0.1)
        # if i % 2 == 0
        #     τ[:,i+1] = conv(τ[:,i+1], h)[Mh+1:end-Mh]
        # end
    end
    pulses = [x > 0.5 ? 1 : -1 for x in v[:,1]]
    pre = reverse([
            1,-1, 1,-1, -1,-1,
           -1,1, -1,1, -1,-1,
           -1,-1, -1,-1])
    cor = conv(pulses, pre)[16:end-15]
    i = findfirst(cor .== 16)
    if i === nothing
        println("Preamble not found")
        println(nothing)
        #return nothing

        i = 1
    end
    pi = i+16:i+2*112+15
    pulses = reshape(pulses[pi],2,:)
    i = i:i+2*112+21
    b = pulses[1,:] .== 1
    b0 = pulses[2,:] .== -1
    m = decode(b)
    m0 = decode(b0)
    #i = 1:length(pulses)
    println((m, crc(m), unpack(m)))
    #return (m, crc(m), unpack(m))
    println((m0, crc(m0), unpack(m0)))
    t = calc_t(N)
    ft = [t + p/40 for p in -10:10, t in t]
    fy = [y(t + p/40) for p in -10:10, t in t]
    #t += τ[:,end]
    return plot(
            begin
                #plot(t[i], v[i,1], marker = :x, size=(1600, 800), label = "y", ticks = :native)
                #plot!(t[i], v[i,2], marker = :x, size=(1600, 800))
                plot(ft[:,i][:], [y[1] > 0.5 ? 1 : 0 for y in fy[:,i][:]], label = "yf", size=(1600, 800), linewidth = 3, ticks = :native)
                plot!(ft[:,i][:], [y[2] for y in fy[:,i][:]], label = "y'f")
                plot!(ft[:,i][:], [y[3] for y in fy[:,i][:]], label = "y''f", alpha = 0.5)
                #plot!(t, rate[:,end], alpha = 0.5, label = "rate")
                #plot!(t, τ[:,end], alpha = 0.5, label = "τ")
                #scatter!(t[i], [x > 5e-3 ? 1 : 0 for x in v[i,1:1]], marker = :x, alpha = 0.25)
            end,
            #surface(τ, size=(1600, 800)),
            layout = (1,1))
end

[adsb_demod(x[i:i+500]) for i in bursts[13:13]][1]
[adsb_demod(x[i:i+500]) for i in bursts[1:20]]
let i = bursts[10]
    s = abs2.(x[i:i+500])
    s *= sqrt(2)/maximum(s)
    sa = [s > 0.5 ? 1 : 0 for s in s]
    sa = conv(s, ones(20))[11:end-10]
    plot(s, marker = :x)
    plot!(sa, marker = :x)
    vline!([findfirst(sa .>= 1), findlast(sa .>= 1)])
    #, argmax(x->x >= 4, sa)])
    #,argmax(sa .>= 0.2)])
end
vals = [adsb_demod(x[i:i+500]) for i in bursts[1:10]]
fi = interp(pd, s)
plot(x[bursts[2]:bursts[2]+500].|>db)
make_obj = function(pd::PolyphaseDerivativeFilterBank, s; sps = 6)
    interp = make_interp(pd, s, sps)
    return interp, function(x)
        t = x[2]:x[1]:length(s)-1
        #display(t)
        return sum(sign(y[2])*y[2]^2 for y in interp.(t))
    end
end
fi, obj = make_obj(pd, s)
obj([1.1,0])

a = LinRange(0.499, 0.501, 20)
b = LinRange(0,24,12)
heatmap(a, b, [obj([a,b]) for b in b, a in a])
t = b[2]:a[8]:length(s)
plot([y[1] for y in fi.(t)], marker = :x)
plotlyjs()

h = reshape(
    digitalfilter(Lowpass(1/64), FIRWindow(hamming(64*201))),
    64,:)
plot(fftshift(fftfreq(size(h,2))), abs2.(fftshift(fft(h[1,:]))))
sup = similar(s, size(h,1), length(s))
for i in 1:size(h,1)
    sup[i,:] = conv(h[i,:], s)[Mh+1:end-Mh]
end
plot(db.(sup[:]))

sf = [begin
    l = max(i[2] - Mh, 1)
    u = min(i[2] + Mh, length(s))
    uh = size(h,2) - (l - i[2] + Mh)
    lh = 1 + i[2] + Mh - u
    #display((i,l,u,lh,uh))
    dot(reverse(h[i[1],:][lh:uh]), s[l:u])
end
for i in i_rs]
h = remez(201*33, [(0,2/6)=>(1,1), (2.2/6,0.5)=>(0,1)], Hz = 33)
Mh = size(h,2)÷2
plot(6 .* fftshift(fftfreq(length(s))), hcat((
        conv(h[i,:],s)[Mh+1:end-Mh] for i in 17:17
    )...).|>db)
plot(6 .* fftshift(fftfreq(size(h,2))), db.(fftshift(fft(h[17,:]))),  ticks = :native)
sf = resample(conv(h[1,:],s)[Mh+1:end-Mh],2/sps)
sf = resample(p, s, 8)
function power_db(y)
    return hcat(
        real.(y[:,1] .* conj.(y[:,1])),
        real.(y[:,2] .* conj.(y[:,1]) + y[:,1] .* conj.(y[:,2])),
        real.(y[:,3] .* conj.(y[:,1]) + 2 .* y[:,2] .* conj.(y[:,2]) + y[:,1] .* conj.(y[:,3]))
    )
end
plot(power_db(sf), ticks=:native)
plot(clamp.(-2 .+ 4abs2.(sf), -1, 1))
vline!(51+8*6 .+ (0:6:111*6), line=:dash, alpha=0.5)
vline!(54+8*6 .+ (0:6:111*6), line=:dash, alpha=0.5)

p = zeros(Float64, 24,16)
p[:,:] .= [
    1,-1,1,-1,-1,-1,
    -1,1,-1,1,-1,-1,
    -1,-1,-1,-1]'
p = p[:]
pulses = [x < 0.5 ? -1.0 : 1.0 for x in sfd[:,1]]
plot(xcorr(pulses,p))
t0 = argmax(xcorr(pulses,p)[length(p):end]) + 12 + 8*48
begin
    plot(sfd[:,:])
    vline!(t0:8*6:size(sfd,1))
    vline!(t0+24:8*6:size(sfd,1))
end

plot!(392 .+ (1:length(p)), p)
b = [x > 0.5 ? 1 : 0 for x in sfd[t0:48:t0+111*48]]
c = [x > 0.5 ? 0 : 1 for x in sfd[t0+24:48:t0+111*48+24]]

decode = function(b)
    n = UInt128(0)
    for _b in b
        n <<= 1
        if _b == 1
            n |= 0x1
        end
    end
    return n
    #sum(_b*(UInt128(0x1)<<(i-1)) for (i,_b) in enumerate(reverse(b)))
end

m = decode(b)
crc = function(M)
    G = UInt128(0x1fff409) << (112-25)
    mask0 = UInt128(0x1000000)<<(112-25)
    mask1 = UInt128(0x1FFFFFF) << (112-25)
    for i in 112:-1:25
        if (0 != mask0 & M)
            M = ~mask1 & M + xor(mask1 & M, G)
        end
        mask0 >>= 1
        mask1 >>= 1
        G >>= 1
    end
    return M
end
b = v[1,:] == -1
crc(decode(b))

unpack = function(m)
    getbits = function(n)
        mask = (UInt128(0x1) << n) - 1
        val = m & mask
        m >>= n
        return val
    end
    return (
        pi = UInt32(getbits(24)),
        me = (
            tc = Int8(getbits(5)),
            val = UInt64(getbits(51))
        ),
        icao = UInt32(getbits(24)),
        ca = Int8(getbits(3)),
        df = Int8(getbits(5))
    )
end