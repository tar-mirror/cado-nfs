
/* TODO: report this to Allan:
 *
 * > Vector(GF(11),Matrix(2,2,[GF(11)|1,2,3,4]));
 * ( 1  2)
 * ( 3  4)
 *
 */

/* This magma script must be run from the directory containing the data files
 * from a bwc run (converted to magma format by bwc-ptrace.sh */


load "mn.m";
load "t.m";
M:=var;
M_orig:=M;

/* We'll pad the matrix to something which has nice dimensions, multiples of
 * nh*nv. Otherwise I'll get crazy very soon */
load "placemats.m";
/* Now M is M_orig zero-padded in rows & cols */
/* while Mt is the matrix as dispatched on the different threads/jobs */

KP<X>:=PolynomialRing(GF(p));

if p eq 2 then
    g:=func<var|Transpose(Matrix([Vector(GF(2),Intseq(x,2,64)):x in var]))>;
else
    function group(nlimbs)
        return func<var|[Seqint([x mod 2^64:x in var[i*k+1..i*k+k]],2^64):i in [0..#var div k - 1]] where k is nlimbs>;
    end function;
    plimbs:=Ceiling(Log(2^64,p));
    g:=group(plimbs);
end if;

splitwidth:=p eq 2 select 64 else 1;

// assert nc le nr;
// nc:=nr;
// x:=Matrix(GF(p),nr,nc,[]);InsertBlock(~x,M,1,1);M:=x;

load "rw.m"; rw:=var;
load "cw.m"; cw:=var;

load "b.m"; 
nr_orig:=nr;
nc_orig:=nc;
nr:=tr;
nc:=tc;
load "vectorspace.m";

/*
//L:=[]; for i in [0..nh-1] do
//t:=nr div nh + (i lt nr mod nh select 1 else 0); 
//s:=i*nrp;
//L cat:=[s+1..s+t];
//end for;Lr:=L;
//for j in [1..#Lr] do Cr[Lr[j]][j]:=1; end for;
Cr:=Matrix(GF(p),nh*nrp,nr,[]);
InsertBlock(~Cr,IdentityMatrix(GF(p), Minimum(nh*nrp,nr)),1,1);
//L:=[]; for j in [0..nv-1] do
//t:=nc  div nv + (j lt nc mod nv select 1 else 0);
//s:=j*ncp;
//L cat:=[s+1..s+t];
//end for;Lc:=L;
// for j in [1..#Lc] do Cc[Lc[j]][j]:=1; end for;
Cc:=Matrix(GF(p),nv*ncp,nc,[]);
InsertBlock(~Cc,IdentityMatrix(GF(p),Minimum(nv*ncp,nc)),1,1);

function is_almost_one(M, n)
    if not IsDiagonal(M) then return false; end if;
    for i in [1..n] do if M[i,i] ne 1 then return false; end if; end for;
    return true;
end function;

assert is_almost_one(Transpose(Cr)*Cr, Minimum(nh*nrp,nr));
assert is_almost_one(Transpose(Cc)*Cc, Minimum(nv*ncp,nc));
*/

ncx := nv*ncp;
nrx := nh*nrp;

nz:=nrp div nv;
assert nz eq ncp div nh;
pr:=func<x|(qr*nh+qq)*nz+r+1 where qq,qr is Quotrem(q, nv) where q,r is Quotrem(x-1,nz)>;
prinv:=func<x|(qr*nv+qq)*nz+r+1 where qq,qr is Quotrem(q, nh) where q,r is Quotrem(x-1,nz)>;
prp:=SymmetricGroup(nrx)![pr(x):x in [1..nrx]];
Pr:=PermutationMatrix(GF(p),[pr(x):x in [1..nrx]]);
assert Pr eq PermutationMatrix(GF(p),prp);



sc:=SymmetricGroup(nv*ncp)!colperm;
if assigned rowperm then
    sr:=SymmetricGroup(nh*nrp)!rowperm;
else
    sr:=SymmetricGroup(nh*nrp)!colperm;
end if;
sr_inv:=sr^-1;
sc_inv:=sc^-1;
Sr:=PermutationMatrix(GF(p),sr);
Sc:=PermutationMatrix(GF(p),sc);
if assigned rowperm then
    Sr:=Pr^-1*Sr;
    sr:=prp^-1*sr;
end if;

// Mx:=Cr*M*Transpose(Cc);
Mx:=M;

assert Mx[2] eq (Sr*Mx)[2^(sr^-1)];
assert Transpose(Mx)[2] eq (Sc*Transpose(Mx))[2^(sc^-1)];

S:=Sr;
P:=Pr;

// de-correlation permutation:
q:=SymmetricGroup(ncx)![preshuf(x):x in [1..ncx]];
Q:=PermutationMatrix(GF(p),q);
q0:=SymmetricGroup(nc)![preshuf(x):x in [1..nc]];
Q0:=PermutationMatrix(GF(p),q0);

/* XXX Does this hold, really ? */
assert Transpose(Pr*Sr)*Mt*Sc eq Mx*Q;
// , " (this boolean may sometimes be wrong, I think)";
// t means twisted.

/* Note that Mt being equal to Pr times Sr*Cr*M*Transpose(Cc)*Sc^-1 is a
 * direct consequence of the fact that balancing_workhorse.c permutes the
 * rows which are being sent.
 */
if Mt eq P*S*Mx*Q*S^-1 then
    print "Shuffled product detected";
elif  Mt eq S*Mx*Q*S^-1 then
    print "non-shuffled product detected";
    /* This has been deprecated */
    assert false;
    Pr:=Identity(Parent(Pr));
else
    assert false;
end if;
assert Transpose(Pr*Sr)*Mt*Sc eq Mx*Q;
assert Mt eq Pr*Sr*Mx*Q*Sc^-1;

/****************************/

colweights:=func<M|[#[i:i in [1..Nrows(M)]|M[i,j] ne 0]: j in [1..Ncols(M)]]>;
rowweights:=func<M|[#[j:j in [1..Ncols(M)]|M[i,j] ne 0]: i in [1..Nrows(M)]]>;
// colsums:=func<M|[&+[Integers()!M[i,j]:i in [1..Nrows(M)]]: j in [1..Ncols(M)]]>;
// rowsums:=func<M|[&+[Integers()!M[i,j]:j in [1..Ncols(M)]]: i in [1..Nrows(M)]]>;

assert colweights(Mt*Sc) eq colweights(Mx*Q);
// assert rowweights(Transpose(Pr*Sr*Cr)*Mt) eq rowweights(M);
assert rowweights(Transpose(Pr*Sr)*Mt) eq rowweights(M);


assert IsZero((Mx*Q)[nr_orig+1..nr]);
assert IsZero(Transpose(Mx*Q)[nc_orig+1..nc]);



/* The matrix by which we multiply, really */

// assert Submatrix(Transpose(Mx*Q),1,1,nr_orig,nr_orig) eq Submatrix(Transpose(Q),1,1,nr_orig,nr_orig)*Transpose(Matrix(M_orig));

assert Submatrix(Transpose(Mx*Q),1,1,nr_orig,nr_orig) eq Submatrix(Transpose(Q),1,1,nr_orig,nr_orig)*Transpose(Matrix(GF(p),nr_orig,nr_orig,Eltseq(M_orig)));


Qsmall:=Submatrix(Q,1,1,nr_orig,nr_orig);
// Msmall:=Matrix(GF(p),Matrix(M_orig));
Msmall:=Matrix(GF(p),nr_orig,nr_orig,Eltseq(M_orig));

MQsmall:=Msmall*Qsmall;

/* MM is really the matrix with which we are going to work. It's MQsmall in
 * the case nullspace=right, which was the first-class citizen being thought
 * of as this script was written at first.
 */
if nullspace eq "left" then
    MM:=Transpose(MQsmall);
else
    MM:=MQsmall;
end if;



// load "R.m";

// truncvec:=func<x|Vector(Eltseq(x)[1..Maximum(nr_orig, nc_orig)])>;
// expandvec:=func<x|Vector(xx cat [0:i in [#xx+1..nr]]) where xx is Eltseq(x)>;

load "VV.m";
RHS:=VV[1][1..nrhs];

/* Only pick first X, since we only want to verify the check vector */
load "x.m";
nchecks:=splitwidth;
Xt:=Matrix(GF(p),nchecks,nr_orig,[]);
for i in [1..nchecks] do for u in var[i] do Xt[i,u] +:=1; end for; end for;

function canvec(V,i) x:=V!0; x[i]:=1; return x; end function;

Xfull:=Matrix(GF(p),#var,nr_orig,[]);
for i in [1..#var] do for u in var[i] do Xfull[i,u] +:=1; end for; end for;

x0:=Rows(Xfull);
print "Dimension of the span of the vectors of X is", Dimension(sub<Universe(x0)|x0>);
xi:=[v*MM : v in x0];


// if System("test -f C0.m") eq 0 then
// magma can't do if (...) then load "..."; (sigh).
load "C0.m";
if Category(var) ne RngIntElt then
    C0:=VS!g(var);
elif assigned C0 then
    delete C0;
end if;
//load "C1.m";
//if Category(var) ne RngIntElt then
//    C1:=Vector(GF(p),nr_orig,g(var));
//elif assigned C1 then
//    delete C1;
//end if;
// load "C50.m"; C50:=Vector(GF(p),nr_orig,g(var));

/*
M2:=MM*MM;
M4:=M2*M2;
M8:=M4*M4;
xi:=x0;
xblock:=[Universe(x0)|];
for i in [1..8] do
    xblock cat:= xi;
    xi:=[v*MM : v in xi];
end for;
MX:=Matrix(xblock);


MXN:=MX;
i:=0;
rr:=[Universe(Rows(MX))|];
while i * m lt Nrows(MM) do
    rr cat:=Rows(MXN);
    MXN:=MXN*M8;
    i+:=8;
    printf ".";
    if i le 32 or i mod 128 eq 0 or (i ge 240 and i mod 32 eq 0) then
        printf "\n";
        time U:=sub<Universe(x0)|rr>;
        time d:=Dimension(U);
        print i, d, i*m-d;
    end if;
end while;

*/


// interval:=50;
// interval:=1;

//if assigned C1 then
//    print "Checking consistency of check vector C1";
//    assert C1 eq xi[1];
//    // C1 eq Vector(Xt)*Transpose(MM);
//    print "Checking consistency of check vector C1: done";
//end if;
//// end if;

load "Ci.m";
if assigned C0 then
    print "Checking consistency of check vector C.0";
    assert C0 eq VS!Xt;
    print "Checking consistency of check vector C.0: done";
    printf "Checking consistency of check vector C.%o\n", interval;
    if Category(var) ne RngIntElt then
        Ci:=VS!g(var);
    elif assigned Ci then
        delete Ci;
    end if;
    foo:=C0;
    for i in [1..interval] do foo *:= MM; end for;
    assert Ci eq foo;
    printf "Checking consistency of check vector C.%o: done\n", interval;
end if;

print "Checking consistency of all vectors V_i";
for i in [1..#VV-1] do
    vx:=VerticalJoin(VV[i]);
    for j in [1..interval] do vx := vx*Transpose(MM); end for;
    assert vx eq VerticalJoin(VV[i+1]);
end for;
print "Checking consistency of all vectors V_i: done";





mcoeff:=func<M,k|
ChangeRing(Parent(M)![Coefficient(x,k):x in Eltseq(M)],BaseRing(BaseRing(M)))>;
mdiv:=func<M,k|Parent(M)![k ge 0 select x div BaseRing(M).1^k else x: x in Eltseq(M)]>;
mmulx:=func<M,k|Parent(M)![x * BaseRing(M).1^k: x in Eltseq(M)]>;
mmod:=func<M,k|Parent(M)![x mod BaseRing(M).1^k: x in Eltseq(M)]>;
mval:=func<M|Minimum([Valuation(x):x in Eltseq(M)])>;
mdeg:=func<M|Maximum([Degree(x):x in Eltseq(M)])>;
mcol:=func<M,j|Transpose(M)[j]>;
mrow:=func<M,i|M[i]>;
function reciprocal(P,k)
    assert k ge Degree(P);
    return Parent(P)![Coefficient(P, k-i):i in [0..k]];
end function;

function mpol_eval(y,M,P)
    /* compute y * P(M) */
    assert CoefficientRing(P) cmpeq CoefficientRing(M);
    s:=Parent(y)!0;
    if Degree(P) lt 0 then return s; end if;
    i:=Degree(P);
    s:=Coefficient(P, i) * y;
    while i gt 0 do
        s:=s*M;
        i:=i-1;
        s+:=Coefficient(P, i) * y;
    end while;
    return s;
end function;

/* Same, but for y a sequence of starting vectors (in the form of a matrix)
 * and P a matrix of polynomials.
 *
 * We want the different vectors in y be matched with the rows of P, so that
 * we'll do a Transpose() behind the scenes.
 * */
function mpol_eval_polmat(y,M,P)
    if splitwidth eq 1 then
        return mpol_eval(y,M,P[1,1]);
    end if;
    /* compute y * P(M) */
    assert BaseRing(CoefficientRing(P)) cmpeq CoefficientRing(M);
    s:=Parent(y)!0;
    if mdeg(P) lt 0 then return s; end if;
    i:=mdeg(P);
    s:=mcoeff(Transpose(P), i) * y;
    while i gt 0 do
        s:=s*M;
        i:=i-1;
        s+:=mcoeff(Transpose(P), i) * y;
    end while;
    return s;
end function;



load "A.m";
function matpol_from_sequence(seq, m, n)
    if p gt 2 then
        assert #seq mod m*n eq 0;
        deg:=#seq div (m*n)-1;
        F:=&+[KP.1^i*      Matrix(KP,m,n,seq[i*m*n+1..(i+1)*m*n]):i in [0..deg]];
        // Fr:= &+[KP.1^(deg-i)*Matrix(KP,m,n,seq[i*m*n+1..(i+1)*m*n]):i in [0..deg]];
    else
        assert Nrows(seq)*Ncols(seq) mod m*n eq 0;
        deg:=Nrows(seq)*Ncols(seq) div (m*n)-1;
        // it's quite messy...
        tmp:=Matrix((deg+1)*m, n, Eltseq(Transpose(seq)));
        F:=&+[KP.1^i*Matrix(KP,Submatrix(tmp,i*m+1,1,m,n)):i in [0..deg]];
    end if;
    return F;
end function;
A:=matpol_from_sequence(g(var),m,n);

print "Checking consistency of A";
for k in [0..9] do
    if k*interval gt mdeg(A) then break; end if;
    vx:=VerticalJoin(VV[k+1]);
    assert mcoeff(A, k*interval) eq Matrix(m,n,[(x0[i],vx[j]):j in [1..n], i in[1..m]]);
end for;
print "Checking consistency of A: done";


/* Note that bwc-ptrace already uses g() to parse data which goes to Fchunks
 * and Rchunks */
load "Fchunks.m";
print "Reading generator saved in F.*.sols*";
F:=BlockMatrix(
        n div splitwidth,
        n div splitwidth,
        [matpol_from_sequence(Fchunks[i,j],splitwidth,splitwidth)
            :i,j in [1..n div splitwidth]
        ]);
// F:=Matrix(n,n,[Polynomial(GF(p),Fchunks[i][j]): i,j in [1..n]]);
if p gt 2 then
degsF:={#Fchunks[i][j] div splitwidth -1:i,j in [1..n div splitwidth]};
else
degsF:={Ncols(Fchunks[i][j]) div splitwidth -1:i,j in [1..n div splitwidth]};
end if;
assert #degsF eq 1;
degF:=Setseq(degsF)[1];
Fr:=Parent(F)![reciprocal(x,degF):x in Eltseq(F)];
rhscoeffs:=Matrix(GF(p),#RHS,n,[Polynomial(Rchunks[i][j]):
j in [1..n],
i in [1..#RHS]
]);
print "Reading generator saved in F.*.sols*: done";

degA:=mdeg(A);
coldegs:=[mdeg(mcol(Fr,j)):j in [1..n]];

/* At this point, in the case where no "nrhs" argument has been passed to
 * plingen, the computed matrix F should be such that the following predicates
 * hold.
 */


// degF:=mdeg(F);

if #RHS eq 0 then
    print "Checking generator computed by (p)lingen (homogeneous case)";
    AF:=mmod(mdiv(A,1)*Fr,degA);
    assert exists(e) { e : e in [0..4] | &and[IsZero(mdiv(mcol(AF,j),e+coldegs[j])):j in [1..n]]};
    print "Degree offset for generator is", e;
else
    print "Checking generator computed by (p)lingen (inhomogeneous case)";
    /* In this case, we have operated on a matrix A where only the last
     * columns have been chopped off in the computation.
     */
    r:=#RHS;
    At:=Transpose(A);
    Ax:=Transpose(VerticalJoin(Matrix(At[1..r]), mdiv(Matrix(At[#RHS+1..n]),1)));
    /* And Fr is also a bit peculiar, as some rows of it have been shifted out
     * to form rhscoeffs. The rhscoeffs were in the constant coefficient in F,
     * and therefore in the head coefficients in Fr.
     */

    Ax0:=Submatrix(Ax, 1, 1, n, r);
    Ax1:=Submatrix(Ax, 1, r+1, n, n-r);
    assert Ax0 eq Submatrix(A,1,1,n,r);
    assert Ax1 eq mdiv(Submatrix(A,1,r+1, n, n-r),1);

    Fx:=VerticalJoin(rhscoeffs + mmulx(Matrix(F[1..#RHS]),1),Matrix(F[#RHS+1..n]));
    Fx0:=Submatrix(Fx, 1, 1, r, n);
    Fx1:=Submatrix(Fx, r+1, 1, n-r, n);
    assert Fx0 eq rhscoeffs + X*Submatrix(F, 1, 1, r, n);
    assert Fx1 eq Submatrix(F, r+1, 1, n-r, n);

    Frx :=Parent(Fx)![reciprocal(x,degF):x in Eltseq(Fx)];
    Frx0:=Parent(Fx0)![reciprocal(x,degF):x in Eltseq(Fx0)];
    Frx1:=Parent(Fx1)![reciprocal(x,degF):x in Eltseq(Fx1)];
    /* A has an inherent O(X^(degA+1)). Since we've shifted a few columns,
     * it's even O(X^degA).
     */
    AxFx:=mmod(Ax*Frx,degA);
    assert exists(e) { e : e in [0..4] | &and[IsZero(mdiv(mcol(AxFx,j),e+coldegs[j])):j in [1..n]]};
    print "Degree offset for generator is", e;
end if;
print "Checking generator computed by plingen: done";

/* ok. Now think about this a bit more. This means we have many zeroes. So
 * something ought to be the null vector, right ?
 */
print "Checking that we have a solution to our system";
if #RHS gt 0 then
    images:=Transpose(rhscoeffs)*Matrix(RHS);
else
    images:=Matrix(GF(p),n, nr_orig, []);
end if;
solutions:=Parent(images)!0;
z := VerticalJoin(VV[1]);
for k in [0..degF] do
    for j in [1..n] do
        solutions[j]+:=mcoeff(mcol(F,j),k)*z;
    end for;
    z:=z*Transpose(MM);
end for;
zz:=images + solutions * Transpose(MM);
for j in [0..n div splitwidth - 1] do
    j0:=j*splitwidth;
    j1:=j0+splitwidth;
    printf "Checking that sols%o-%o yields a solution... ", j0, j1;
    assert IsZero(zz[j0+1..j1]);
    print " ok";
end for;

load "S.m";
nsols:=#vars * splitwidth;
printf "Computed %o solution vectors with mksol\n", nsols;

// we no longer impose that we compute #RHS solutions. One should be enough
// for everybody.
// assert nsols ge #RHS;

// SSblocks:=[[Vector(GF(p),g(v)):v in w]:w in vars];
SSblocks:=[[VS!g(v):v in w]:w in vars];
SS:=[&+[x:x in y]:y in SSblocks];

function sub_vblock(vv,j)
    if splitwidth eq 1 then
        return vv[1+j];
    else
        return VS!vv[1+j*splitwidth..(j+1)*splitwidth];
    end if;
end function;

function sub_polmat(F,i,j)
    return Submatrix(F,1+i*splitwidth,1+j*splitwidth,splitwidth,splitwidth);
end function;

print "Checking that mksol has computed what we expect";
for i in [0..nsols div splitwidth - 1] do
    vv:=VerticalJoin(V_0);
    for k in [0..#SSblocks[i+1]-1] do
        contribs:=VS!0;
        for j in [0..n div splitwidth - 1] do
            vb:=sub_vblock(vv,j);
            pol:=mmod(mdiv(sub_polmat(F,j,i),k*interval),interval);
            contribs+:=mpol_eval_polmat(vb, Transpose(MM), pol);
        end for;
        assert SSblocks[i+1][k+1] eq contribs;
        for s in [1..interval] do
            vv:=vv*Transpose(MM);
        end for;
    end for;
    assert SS[i+1] eq &+[mpol_eval_polmat(sub_vblock(VerticalJoin(V_0),j),Transpose(MM),sub_polmat(F,j,i)):j in [0..n div splitwidth-1]];
end for;
print "Checking that mksol has computed what we expect: done";

/* all_rhs is the *contribution* of the RHS columns to the sum */
all_rhs:=Transpose(rhscoeffs)*VerticalJoin(RHS);
/* XXX so I think that all_rhs unconditionally has n rows... */
assert Nrows(all_rhs) eq n;

/* here, we're checking only nsols, since we have *computed* only nsols. But
 * had we computed more, we could extend the check somewhat.
 */
for s in [0..nsols div splitwidth - 1] do
    assert IsZero(sub_vblock(all_rhs,s) + (SS[s+1])*Transpose(MM));
end for;

// RHS has the RHS in rows.
Mpad:=HorizontalJoin(Msmall, Transpose(VerticalJoin(RHS)));
ww:=[];
for c in [0..nsols div splitwidth - 1] do
    // v:=&+[mpol_eval(V_0[j],Transpose(MM),F[j,c]): j in [1..n]];
    v:=SS[c+1];
    assert IsZero(sub_vblock(all_rhs,c) + v * Transpose(MM));
    if nullspace eq "right" then
        w0:=v * Transpose(Qsmall);
        if splitwidth eq 1 then
            w1:=Transpose(rhscoeffs)[c+1];
        else
            w1:=Submatrix(Transpose(rhscoeffs),1+c*splitwidth,1,splitwidth,Nrows(rhscoeffs));
        end if;
        assert IsZero(w0 * Transpose(Msmall) + w1*VerticalJoin(RHS));
        w:=Vector(HorizontalJoin(w0,w1));
    else
        // as we've defined things, the inhomogeneous systems are only though
        // of as M*X=B, not X*M=B. 
        assert #RHS eq 0;
        w:=v;
        assert IsZero(w * Msmall);
    end if;
    Append(~ww, w);
end for;

tr:=nullspace eq "left" select func<x|x> else Transpose;
if nullspace eq "left" then
    equation:="w * M";
elif #RHS gt 0 then
    equation:="(M||RHS) * w";
else
    equation:="M * w";
end if;
printf "Check that we have w such that %o = 0: %o\n",
    equation,
    IsZero(VerticalJoin(ww) * tr(Mpad));


print "Checking that gather has computed what we expect";
load "K.m";
ker:=[Vector(GF(p),g(x)):x in vars];

if nullspace eq "right" then
    /* There are two sets of asserts here, and it's admittedly somewhat
     * confusing.  Depending on whether it succeeded or not, gather may put
     * varying stuff in the K.sols file.
     */
    if Dimension(Parent(ker[1])) eq Ncols(Msmall) then
        /* It may occur that gather wasn't happy with the solution it got for
         * the inhomogenous linear system. In that case, the binary K file
         * misses the rhs coefficients, which is weird. So then, we just have
         * the following:
         */
        assert Matrix(ker) eq Matrix(SS)*Transpose(Qsmall);  
        assert IsZero(
            HorizontalJoin(Matrix(ker),Matrix(Transpose(rhscoeffs)[1..#ker])) *
            VerticalJoin(Transpose(Msmall), Matrix(RHS)));
    elif Dimension(Parent(ker[1])) eq Ncols(Msmall) + #RHS then
        /* In contrast, when it is happy, the K file has nrhs extra
         * coordinates, so that the following holds...
         */
        assert IsZero(Matrix(ker)*Transpose(HorizontalJoin(Msmall, Transpose(Matrix(RHS)))));
    else
        /* uh oh */
        error "I don't understand the dimension of the sequence computed by gather in K.m";
    end if;
    /* For completeness, note that the ascii K file is almost identical to the
     * above except that we acknowledge there the presence of zero columns in
     * M, so that a handful of coefficients in ker are useless, and hence
     * chopped out.
     */
else
    /* Then it's simpler, because we have no RHS to mess things up */
    assert IsZero(VerticalJoin(ker)*Msmall);
end if;
print "Checking that gather has computed what we expect: done";

