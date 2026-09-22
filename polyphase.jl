using DSP, LinearAlgebra, Statistics

"""
    PolyphaseFilterBank([T=Float64], M, Np, Nd = 0)

Creates a polyphase filter bank with `M` taps in each of the `Np` banks.
If `Nd > 0` then also adds banks for the first & second derivatives by applying
`2*Nd+1` point centered difference stencils to each of the filter banks individually.
"""
struct PolyphaseFilterBank{T<:Real}
    h::Array{T,3}
end

function PolyphaseFilterBank(::Type{T}, M, Np, Nd = 0) where T<:Real
    s = -Nd:Nd
    A = [s[i].^d for d in 0:2Nd, i in 1:length(s)]
    window = reshape(
            digitalfilter(Lowpass(1/Np),
            FIRWindow(hanning(Np*M))),
            Np,M)
    h = zeros(T, M + 2Nd, Np, Nd == 0 ? 1 : 3)
    for j in 1:size(h,3)
        b = zeros(T, 2Nd+1)
        b[j] = factorial(j-1)
        c = A \ b
        for pi in 1:Np
            h[:,pi,j] = Np.*xcorr(view(window, pi,:), c)
        end
    end
    PolyphaseFilterBank(h)
end
PolyphaseFilterBank(M, Np, Nd = 0) = PolyphaseFilterBank(Float64, M, Np, Nd)


function resample(p::PolyphaseFilterBank, x::AbstractVector{T}, rate::Real) where T<:Number
    N = length(x)
    Mh = size(p.h,1)÷2
    Np = size(p.h,2)
    ts = Np÷2:Np/rate:N*Np-Np÷2
    out = zeros(T, length(ts), size(p.h,3))
    for (oi,t) in enumerate(Np÷2:1/rate*Np:N*Np-Np÷2)
        ti = Int(round(t)) - 1
        pi, i = 1 + ti % Np, 1 + ti ÷ Np
        for k in 1:size(p.h,3)
            for m in -Mh:Mh
                if 1 <= i + m <= N
                    out[oi,k] += p.h[Mh-m+1,pi,k] * x[i+m]
                end
            end
        end
    end
    return out
end

function make_interp(p::PolyphaseFilterBank, s, sps)
    T = eltype(s)
    N = length(s)
    Np = size(p.h,1)
    M = size(p.h,2)
    Mh = M÷2
    cinds = CartesianIndices((Np, N))
    return function(t)
        ti = cinds[clamp(1+Int(round(Np*sps*t)), 1, length(cinds))]
        y = sum(
            p.h[ti[1],Mh-m+1,:] .* s[ti[2]+m]
            for m in -Mh:Mh if 1 <= ti[2] + m <= N)
        return [
            real(y[1]*conj(y[1])),
            real(y[1]*conj(y[2]) + conj(y[1])*y[2]),
            real(y[1]*conj(y[3]) + 2conj(y[2])*y[2] + conj(y[1])*y[3])
        ]
    end
end
